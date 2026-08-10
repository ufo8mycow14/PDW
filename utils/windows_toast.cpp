#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <activation.h>
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
#include <wrl/client.h>

#include "windows_toast.h"

#include <cwchar>
#include <sstream>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace ABI::Windows::Data::Xml::Dom;
using namespace ABI::Windows::UI::Notifications;

namespace pdw
{
namespace outputs
{

struct WindowsToast::State
{
	typedef HRESULT (WINAPI *RoInitializeFunction)(DWORD);
	typedef void (WINAPI *RoUninitializeFunction)();
	typedef HRESULT (WINAPI *RoGetActivationFactoryFunction)(HSTRING, REFIID, void**);
	typedef HRESULT (WINAPI *WindowsCreateStringFunction)(PCNZWCH, UINT32, HSTRING*);
	typedef HRESULT (WINAPI *WindowsDeleteStringFunction)(HSTRING);

	State()
		: module(NULL), roInitialize(NULL), roUninitialize(NULL), roGetActivationFactory(NULL),
		  windowsCreateString(NULL), windowsDeleteString(NULL), initialized(false),
		  shouldUninitialize(false) {}

	HMODULE module;
	RoInitializeFunction roInitialize;
	RoUninitializeFunction roUninitialize;
	RoGetActivationFactoryFunction roGetActivationFactory;
	WindowsCreateStringFunction windowsCreateString;
	WindowsDeleteStringFunction windowsDeleteString;
	bool initialized;
	bool shouldUninitialize;
};

namespace
{
	const wchar_t APP_USER_MODEL_ID[] = L"PDW.PagingDecoder";

	std::string HResultText(const char* action, HRESULT result)
	{
		std::ostringstream output;
		output << action << " (Windows error 0x" << std::hex << static_cast<unsigned long>(result) << ").";
		return output.str();
	}

	std::wstring Utf8ToWide(const std::string& value)
	{
		if (value.empty()) return std::wstring();
		const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			value.data(), static_cast<int>(value.size()), NULL, 0);
		if (length <= 0) return std::wstring();
		std::vector<wchar_t> result(static_cast<std::size_t>(length));
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
			static_cast<int>(value.size()), &result[0], length);
		return std::wstring(result.begin(), result.end());
	}

	std::wstring XmlEscape(const std::wstring& value)
	{
		std::wstring result;
		result.reserve(value.size());
		for (std::wstring::const_iterator character = value.begin(); character != value.end(); ++character)
		{
			switch (*character)
			{
				case L'&': result += L"&amp;"; break;
				case L'<': result += L"&lt;"; break;
				case L'>': result += L"&gt;"; break;
				case L'\"': result += L"&quot;"; break;
				case L'\'': result += L"&apos;"; break;
				default:
					if (*character >= 0x20 || *character == L'\t' || *character == L'\r' || *character == L'\n')
						result += *character;
			}
		}
		return result;
	}

	HSTRING MakeString(WindowsToast::State* state, const wchar_t* value)
	{
		HSTRING result = NULL;
		if (state && state->windowsCreateString && value)
			state->windowsCreateString(value, static_cast<UINT32>(wcslen(value)), &result);
		return result;
	}

	void FreeString(WindowsToast::State* state, HSTRING value)
	{
		if (state && state->windowsDeleteString && value) state->windowsDeleteString(value);
	}

	void RegisterApplicationIdentity()
	{
		wchar_t executable[MAX_PATH] = {};
		GetModuleFileNameW(NULL, executable, _countof(executable));
		HKEY key = NULL;
		if (RegCreateKeyExW(HKEY_CURRENT_USER,
			L"Software\\Classes\\AppUserModelId\\PDW.PagingDecoder", 0, NULL,
			REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS)
		{
			static const wchar_t displayName[] = L"PDW Paging Decoder";
			RegSetValueExW(key, L"DisplayName", 0, REG_SZ,
				reinterpret_cast<const BYTE*>(displayName), sizeof(displayName));
			wchar_t icon[MAX_PATH + 8] = {};
			swprintf_s(icon, _countof(icon), L"%s,0", executable);
			RegSetValueExW(key, L"IconUri", 0, REG_SZ,
				reinterpret_cast<const BYTE*>(icon), static_cast<DWORD>((wcslen(icon) + 1) * sizeof(wchar_t)));
			DWORD showInSettings = 1;
			RegSetValueExW(key, L"ShowInSettings", 0, REG_DWORD,
				reinterpret_cast<const BYTE*>(&showInSettings), sizeof(showInSettings));
			RegCloseKey(key);
		}

		typedef HRESULT (WINAPI *SetAumidFunction)(PCWSTR);
		HMODULE shell = GetModuleHandleW(L"shell32.dll");
		SetAumidFunction setAumid = shell ? reinterpret_cast<SetAumidFunction>(
			GetProcAddress(shell, "SetCurrentProcessExplicitAppUserModelID")) : NULL;
		if (setAumid) setAumid(APP_USER_MODEL_ID);
	}
}

WindowsToast::WindowsToast() : state_(new State) {}

WindowsToast::~WindowsToast()
{
	Shutdown();
	delete state_;
	state_ = NULL;
}

bool WindowsToast::Initialize(std::string& error)
{
	error.clear();
	if (state_->initialized) return true;
	state_->module = LoadLibraryW(L"runtimeobject.dll");
	if (!state_->module)
	{
		error = "Windows notifications are not available on this Windows version.";
		return false;
	}
	state_->roInitialize = reinterpret_cast<State::RoInitializeFunction>(GetProcAddress(state_->module, "RoInitialize"));
	state_->roUninitialize = reinterpret_cast<State::RoUninitializeFunction>(GetProcAddress(state_->module, "RoUninitialize"));
	state_->roGetActivationFactory = reinterpret_cast<State::RoGetActivationFactoryFunction>(GetProcAddress(state_->module, "RoGetActivationFactory"));
	state_->windowsCreateString = reinterpret_cast<State::WindowsCreateStringFunction>(GetProcAddress(state_->module, "WindowsCreateString"));
	state_->windowsDeleteString = reinterpret_cast<State::WindowsDeleteStringFunction>(GetProcAddress(state_->module, "WindowsDeleteString"));
	if (!state_->roInitialize || !state_->roGetActivationFactory ||
		!state_->windowsCreateString || !state_->windowsDeleteString)
	{
		error = "Windows notification runtime functions are unavailable.";
		Shutdown();
		return false;
	}
	const HRESULT initialized = state_->roInitialize(1); // RO_INIT_MULTITHREADED
	if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
	{
		error = HResultText("Windows notification runtime could not initialize", initialized);
		Shutdown();
		return false;
	}
	state_->shouldUninitialize = SUCCEEDED(initialized);
	RegisterApplicationIdentity();

	HSTRING managerName = MakeString(state_, L"Windows.UI.Notifications.ToastNotificationManager");
	ComPtr<IToastNotificationManagerStatics> manager;
	const HRESULT factoryResult = state_->roGetActivationFactory(managerName, IID_PPV_ARGS(&manager));
	FreeString(state_, managerName);
	if (FAILED(factoryResult) || !manager)
	{
		error = HResultText("Windows notification manager is unavailable", factoryResult);
		Shutdown();
		return false;
	}
	state_->initialized = true;
	return true;
}

bool WindowsToast::Show(const std::string& utf8Title, const std::string& utf8Body, std::string& error)
{
	if (!Initialize(error)) return false;
	const std::wstring title = XmlEscape(Utf8ToWide(utf8Title));
	const std::wstring body = XmlEscape(Utf8ToWide(utf8Body));
	const std::wstring xml = L"<toast><visual><binding template=\"ToastGeneric\"><text>" + title +
		L"</text><text>" + body + L"</text></binding></visual></toast>";

	HSTRING xmlClassName = MakeString(state_, L"Windows.Data.Xml.Dom.XmlDocument");
	ComPtr<IActivationFactory> xmlFactory;
	HRESULT result = state_->roGetActivationFactory(xmlClassName, IID_PPV_ARGS(&xmlFactory));
	FreeString(state_, xmlClassName);
	if (FAILED(result) || !xmlFactory) { error = HResultText("Toast XML factory failed", result); return false; }
	ComPtr<IInspectable> xmlInspectable;
	result = xmlFactory->ActivateInstance(&xmlInspectable);
	if (FAILED(result) || !xmlInspectable) { error = HResultText("Toast XML document failed", result); return false; }
	ComPtr<IXmlDocumentIO> xmlIo;
	result = xmlInspectable.As(&xmlIo);
	if (FAILED(result) || !xmlIo) { error = HResultText("Toast XML interface failed", result); return false; }
	HSTRING xmlText = MakeString(state_, xml.c_str());
	result = xmlIo->LoadXml(xmlText);
	FreeString(state_, xmlText);
	if (FAILED(result)) { error = HResultText("Toast XML could not be loaded", result); return false; }
	ComPtr<IXmlDocument> document;
	result = xmlInspectable.As(&document);
	if (FAILED(result) || !document) { error = HResultText("Toast document interface failed", result); return false; }

	HSTRING managerClassName = MakeString(state_, L"Windows.UI.Notifications.ToastNotificationManager");
	ComPtr<IToastNotificationManagerStatics> manager;
	result = state_->roGetActivationFactory(managerClassName, IID_PPV_ARGS(&manager));
	FreeString(state_, managerClassName);
	if (FAILED(result) || !manager) { error = HResultText("Toast manager failed", result); return false; }
	HSTRING appId = MakeString(state_, APP_USER_MODEL_ID);
	ComPtr<IToastNotifier> notifier;
	result = manager->CreateToastNotifierWithId(appId, &notifier);
	FreeString(state_, appId);
	if (FAILED(result) || !notifier) { error = HResultText("Toast notifier failed", result); return false; }

	HSTRING notificationClassName = MakeString(state_, L"Windows.UI.Notifications.ToastNotification");
	ComPtr<IToastNotificationFactory> notificationFactory;
	result = state_->roGetActivationFactory(notificationClassName, IID_PPV_ARGS(&notificationFactory));
	FreeString(state_, notificationClassName);
	if (FAILED(result) || !notificationFactory) { error = HResultText("Toast notification factory failed", result); return false; }
	ComPtr<IToastNotification> notification;
	result = notificationFactory->CreateToastNotification(document.Get(), &notification);
	if (FAILED(result) || !notification) { error = HResultText("Toast notification could not be created", result); return false; }
	result = notifier->Show(notification.Get());
	if (FAILED(result)) { error = HResultText("Windows could not show the notification", result); return false; }
	error.clear();
	return true;
}

void WindowsToast::Shutdown()
{
	if (!state_) return;
	state_->initialized = false;
	if (state_->shouldUninitialize && state_->roUninitialize) state_->roUninitialize();
	state_->shouldUninitialize = false;
	state_->roInitialize = NULL;
	state_->roUninitialize = NULL;
	state_->roGetActivationFactory = NULL;
	state_->windowsCreateString = NULL;
	state_->windowsDeleteString = NULL;
	if (state_->module) FreeLibrary(state_->module);
	state_->module = NULL;
}

bool WindowsToast::IsAvailable() const
{
	return state_ && state_->initialized;
}

} // namespace outputs
} // namespace pdw
