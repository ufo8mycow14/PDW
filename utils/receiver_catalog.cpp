#ifndef STRICT
#define STRICT 1
#endif

#include "receiver_catalog.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace pdw
{
namespace signal
{

namespace
{
	const char* const RECEIVER_SECTION = "Receiver";
	const char* const STANDARD_RECEIVER_ID = "rtl-sdr-standard";

	std::string JoinPath(const std::string& left, const std::string& right)
	{
		if (left.empty()) return right;
		if (right.empty()) return left;
		if (left[left.size() - 1] == '\\' || left[left.size() - 1] == '/') return left + right;
		return left + "\\" + right;
	}

	std::string DirectoryName(const std::string& path)
	{
		const std::string::size_type separator = path.find_last_of("\\/");
		return separator == std::string::npos ? std::string() : path.substr(0, separator);
	}

	std::string FileName(const std::string& path)
	{
		const std::string::size_type separator = path.find_last_of("\\/");
		return separator == std::string::npos ? path : path.substr(separator + 1);
	}

	std::string FileStem(const std::string& path)
	{
		std::string name = FileName(path);
		const std::string::size_type dot = name.find_last_of('.');
		if (dot != std::string::npos) name.erase(dot);
		return name;
	}

	bool IsRegularFile(const std::string& path)
	{
		const DWORD attributes = GetFileAttributesA(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES &&
			!(attributes & FILE_ATTRIBUTE_DIRECTORY) &&
			!(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
	}

	bool IsDirectory(const std::string& path)
	{
		const DWORD attributes = GetFileAttributesA(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES &&
			(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
			(attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
	}

	WORD ExpectedReceiverMachine()
	{
#if defined(_WIN64)
		return IMAGE_FILE_MACHINE_AMD64;
#else
		return IMAGE_FILE_MACHINE_I386;
#endif
	}

	const char* ReceiverArchitectureLabel()
	{
#if defined(_WIN64)
		return "64-bit";
#else
		return "32-bit";
#endif
	}

	bool IsMatchingWindowsLibrary(const std::string& path)
	{
		HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE) return false;
		IMAGE_DOS_HEADER dosHeader = {0};
		DWORD read = 0;
		bool valid = ReadFile(file, &dosHeader, sizeof(dosHeader), &read, NULL) != FALSE &&
			read == sizeof(dosHeader) && dosHeader.e_magic == IMAGE_DOS_SIGNATURE &&
			dosHeader.e_lfanew > 0;
		DWORD signature = 0;
		IMAGE_FILE_HEADER fileHeader = {0};
		if (valid)
		{
			valid = SetFilePointer(file, dosHeader.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
				ReadFile(file, &signature, sizeof(signature), &read, NULL) != FALSE &&
				read == sizeof(signature) && signature == IMAGE_NT_SIGNATURE &&
				ReadFile(file, &fileHeader, sizeof(fileHeader), &read, NULL) != FALSE &&
				read == sizeof(fileHeader) && fileHeader.Machine == ExpectedReceiverMachine() &&
				(fileHeader.Characteristics & IMAGE_FILE_DLL) != 0;
		}
		CloseHandle(file);
		return valid;
	}

	std::string ReadIniValue(const std::string& iniPath, const char* key)
	{
		char value[512] = {0};
		GetPrivateProfileStringA(RECEIVER_SECTION, key, "", value,
			static_cast<DWORD>(sizeof(value)), iniPath.c_str());
		return value;
	}

	bool IsSafeIdentifier(const std::string& value)
	{
		if (value.empty() || value.size() > 63) return false;
		for (std::size_t index = 0; index < value.size(); ++index)
		{
			const unsigned char character = static_cast<unsigned char>(value[index]);
			if (!std::isalnum(character) && character != '-' && character != '_') return false;
		}
		return true;
	}

	bool IsSafeLibraryName(const std::string& value)
	{
		if (value.empty() || value.size() > MAX_PATH) return false;
		if (value.find("..") != std::string::npos || value.find('\\') != std::string::npos ||
			value.find('/') != std::string::npos || value.find(':') != std::string::npos)
			return false;
		std::string lowered(value);
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		return lowered.size() > 4 && lowered.compare(lowered.size() - 4, 4, ".dll") == 0;
	}

	std::string Slugify(const std::string& value)
	{
		std::string result;
		bool lastWasDash = false;
		for (std::size_t index = 0; index < value.size() && result.size() < 48; ++index)
		{
			const unsigned char character = static_cast<unsigned char>(value[index]);
			if (std::isalnum(character))
			{
				result.push_back(static_cast<char>(std::tolower(character)));
				lastWasDash = false;
			}
			else if (!result.empty() && !lastWasDash)
			{
				result.push_back('-');
				lastWasDash = true;
			}
		}
		while (!result.empty() && result[result.size() - 1] == '-') result.erase(result.size() - 1);
		return result.empty() ? "custom-rtl-sdr" : result;
	}

	HMODULE LoadReceiverLibrary(const std::string& libraryPath, DWORD& loadError)
	{
		loadError = ERROR_SUCCESS;
		if (libraryPath.empty())
		{
			loadError = ERROR_FILE_NOT_FOUND;
			return NULL;
		}
		HMODULE library = LoadLibraryExA(libraryPath.c_str(), NULL,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!library && GetLastError() == ERROR_INVALID_PARAMETER)
		{
			// Windows 7 without KB2533623 does not understand the secure search
			// flags. LOAD_WITH_ALTERED_SEARCH_PATH retains that legacy platform
			// while beginning dependency resolution in the package directory.
			library = LoadLibraryExA(libraryPath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		}
		if (!library) loadError = GetLastError();
		return library;
	}

	bool HasRequiredRtlExports(HMODULE library)
	{
		const char* required[] = {
			"rtlsdr_get_device_count", "rtlsdr_open", "rtlsdr_close",
			"rtlsdr_set_center_freq", "rtlsdr_set_sample_rate",
			"rtlsdr_set_tuner_gain_mode", "rtlsdr_set_tuner_gain",
			"rtlsdr_set_freq_correction", "rtlsdr_reset_buffer",
			"rtlsdr_read_async", "rtlsdr_cancel_async"
		};
		for (std::size_t index = 0; index < sizeof(required) / sizeof(required[0]); ++index)
			if (!GetProcAddress(library, required[index])) return false;
		return true;
	}

	ReceiverPackage ReadPackage(const std::string& directory)
	{
		ReceiverPackage package;
		const std::string iniPath = JoinPath(directory, "receiver.ini");
		if (!IsRegularFile(iniPath)) return package;
		package.id = ReadIniValue(iniPath, "Id");
		package.displayName = ReadIniValue(iniPath, "Name");
		package.description = ReadIniValue(iniPath, "Description");
		const std::string api = ReadIniValue(iniPath, "Api");
		const std::string libraryName = ReadIniValue(iniPath, "Library");
		package.bundled = ReadIniValue(iniPath, "Bundled") == "1";
		if (!IsSafeIdentifier(package.id) || package.displayName.empty() || api != "rtlsdr" ||
			!IsSafeLibraryName(libraryName))
		{
			package.id.clear();
			return package;
		}
		package.libraryPath = JoinPath(directory, libraryName);
		package.compatible = ValidateRtlReceiverLibrary(package.libraryPath, package.status);
		return package;
	}

	void AppendPackagesFromDirectory(const std::string& parent,
		std::vector<ReceiverPackage>& packages, bool recurseOneLevel)
	{
		WIN32_FIND_DATAA data = {0};
		const std::string search = JoinPath(parent, "*");
		HANDLE find = FindFirstFileA(search.c_str(), &data);
		if (find == INVALID_HANDLE_VALUE) return;
		do
		{
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
				(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
				strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
				continue;
			const std::string directory = JoinPath(parent, data.cFileName);
			ReceiverPackage package = ReadPackage(directory);
			if (!package.id.empty()) packages.push_back(package);
			else if (recurseOneLevel) AppendPackagesFromDirectory(directory, packages, false);
		} while (FindNextFileA(find, &data));
		FindClose(find);
	}

	bool WriteReceiverIni(const std::string& directory, const ReceiverPackage& package,
		const std::string& libraryName)
	{
		const std::string iniPath = JoinPath(directory, "receiver.ini");
		return WritePrivateProfileStringA(RECEIVER_SECTION, "Id", package.id.c_str(), iniPath.c_str()) &&
			WritePrivateProfileStringA(RECEIVER_SECTION, "Name", package.displayName.c_str(), iniPath.c_str()) &&
			WritePrivateProfileStringA(RECEIVER_SECTION, "Description", package.description.c_str(), iniPath.c_str()) &&
			WritePrivateProfileStringA(RECEIVER_SECTION, "Api", "rtlsdr", iniPath.c_str()) &&
			WritePrivateProfileStringA(RECEIVER_SECTION, "Library", libraryName.c_str(), iniPath.c_str()) &&
			WritePrivateProfileStringA(RECEIVER_SECTION, "Bundled", "0", iniPath.c_str());
	}

	void RemoveIncompleteImport(const std::string& directory)
	{
		WIN32_FIND_DATAA data = {0};
		HANDLE find = FindFirstFileA(JoinPath(directory, "*").c_str(), &data);
		if (find != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					DeleteFileA(JoinPath(directory, data.cFileName).c_str());
			} while (FindNextFileA(find, &data));
			FindClose(find);
		}
		RemoveDirectoryA(directory.c_str());
	}
}

ReceiverPackage::ReceiverPackage() : bundled(false), compatible(false) {}

RtlSdrDeviceInfo::RtlSdrDeviceInfo() : index(0) {}

std::string GetReceiverRootDirectory()
{
	char modulePath[MAX_PATH] = {0};
	const DWORD length = GetModuleFileNameA(NULL, modulePath, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) return "Receivers";
	return JoinPath(DirectoryName(modulePath), "Receivers");
}

bool ValidateRtlReceiverLibrary(const std::string& libraryPath, std::string& error)
{
	error.clear();
	if (!IsRegularFile(libraryPath))
	{
		error = "Receiver DLL was not found.";
		return false;
	}
	if (!IsMatchingWindowsLibrary(libraryPath))
	{
		error = std::string("Receiver DLL must be a ") + ReceiverArchitectureLabel() +
			" Windows library matching this PDW build.";
		return false;
	}
	DWORD loadError = ERROR_SUCCESS;
	HMODULE library = LoadReceiverLibrary(libraryPath, loadError);
	if (!library)
	{
		char message[160];
		snprintf(message, sizeof(message),
			"Receiver DLL could not be loaded (Windows error %lu). Keep its dependency DLLs in the same receiver folder.",
			static_cast<unsigned long>(loadError));
		error = message;
		return false;
	}
	const bool compatible = HasRequiredRtlExports(library);
	FreeLibrary(library);
	if (!compatible)
	{
		error = "Receiver DLL does not implement the compatible librtlsdr receive API.";
		return false;
	}
	error = "Ready.";
	return true;
}

std::vector<ReceiverPackage> EnumerateRtlReceiverPackages(const std::string& receiverRoot)
{
	std::vector<ReceiverPackage> packages;
	const std::string root = receiverRoot.empty() ? GetReceiverRootDirectory() : receiverRoot;
	AppendPackagesFromDirectory(root, packages, true);

	// Preserve the pre-catalogue behaviour: a DLL already beside PDW remains a
	// valid selectable package and is never moved or replaced.
	const std::string applicationDirectory = DirectoryName(GetReceiverRootDirectory());
	const char* legacyNames[] = { "rtlsdr.dll", "librtlsdr.dll" };
	for (std::size_t index = 0; index < sizeof(legacyNames) / sizeof(legacyNames[0]); ++index)
	{
		const std::string legacyPath = JoinPath(applicationDirectory, legacyNames[index]);
		if (!IsRegularFile(legacyPath)) continue;
		ReceiverPackage legacy;
		legacy.id = "legacy-side-by-side";
		legacy.displayName = "Legacy side-by-side RTL-SDR DLL";
		legacy.description = "Existing receiver DLL beside the PDW executable";
		legacy.libraryPath = legacyPath;
		legacy.compatible = ValidateRtlReceiverLibrary(legacy.libraryPath, legacy.status);
		packages.push_back(legacy);
		break;
	}

	std::sort(packages.begin(), packages.end(), [](const ReceiverPackage& left,
		const ReceiverPackage& right)
	{
		const bool leftStandard = left.id == STANDARD_RECEIVER_ID;
		const bool rightStandard = right.id == STANDARD_RECEIVER_ID;
		if (leftStandard != rightStandard) return leftStandard;
		if (left.displayName != right.displayName) return left.displayName < right.displayName;
		return left.id < right.id;
	});
	return packages;
}

bool ResolveRtlReceiverLibrary(const std::string& receiverId, std::string& libraryPath,
	std::string& error, const std::string& receiverRoot)
{
	libraryPath.clear();
	const std::vector<ReceiverPackage> packages = EnumerateRtlReceiverPackages(receiverRoot);
	for (std::size_t index = 0; index < packages.size(); ++index)
	{
		if (packages[index].id != receiverId) continue;
		if (!packages[index].compatible)
		{
			error = packages[index].status;
			return false;
		}
		libraryPath = packages[index].libraryPath;
		error.clear();
		return true;
	}
	// Older INI files have no receiver ID. Prefer the standard package and then
	// any existing compatible side-by-side package.
	if (receiverId.empty() || receiverId == STANDARD_RECEIVER_ID)
	{
		for (std::size_t index = 0; index < packages.size(); ++index)
		{
			if (packages[index].compatible)
			{
				libraryPath = packages[index].libraryPath;
				error.clear();
				return true;
			}
		}
	}
	error = "Selected receiver package is unavailable. Add it again or select another receiver.";
	return false;
}

std::vector<RtlSdrDeviceInfo> EnumerateRtlSdrDevices(const std::string& libraryPath,
	std::string& error)
{
	std::vector<RtlSdrDeviceInfo> devices;
	if (!ValidateRtlReceiverLibrary(libraryPath, error)) return devices;
	DWORD loadError = ERROR_SUCCESS;
	HMODULE library = LoadReceiverLibrary(libraryPath, loadError);
	if (!library)
	{
		error = "Receiver DLL became unavailable while listing devices.";
		return devices;
	}
	typedef std::uint32_t (__cdecl *CountFunction)();
	typedef const char* (__cdecl *NameFunction)(std::uint32_t);
	typedef int (__cdecl *StringsFunction)(std::uint32_t, char*, char*, char*);
	CountFunction getCount = reinterpret_cast<CountFunction>(GetProcAddress(library, "rtlsdr_get_device_count"));
	NameFunction getName = reinterpret_cast<NameFunction>(GetProcAddress(library, "rtlsdr_get_device_name"));
	StringsFunction getStrings = reinterpret_cast<StringsFunction>(GetProcAddress(library, "rtlsdr_get_device_usb_strings"));
	const std::uint32_t count = getCount ? getCount() : 0;
	for (std::uint32_t index = 0; index < count && index < 256; ++index)
	{
		RtlSdrDeviceInfo info;
		info.index = index;
		if (getName)
		{
			const char* name = getName(index);
			if (name) info.name = name;
		}
		if (getStrings)
		{
			char manufacturer[256] = {0}, product[256] = {0}, serial[256] = {0};
			if (getStrings(index, manufacturer, product, serial) == 0)
			{
				info.manufacturer = manufacturer;
				info.product = product;
				info.serial = serial;
			}
		}
		devices.push_back(info);
	}
	FreeLibrary(library);
	error = devices.empty() ? "Receiver package is ready; no compatible USB receiver is connected." : "Ready.";
	return devices;
}

bool ImportRtlReceiverPackage(const std::string& primaryLibraryPath,
	const std::string& requestedDisplayName, ReceiverPackage& importedPackage,
	std::string& error, const std::string& receiverRoot)
{
	importedPackage = ReceiverPackage();
	if (!ValidateRtlReceiverLibrary(primaryLibraryPath, error)) return false;
	const std::string sourceDirectory = DirectoryName(primaryLibraryPath);
	const std::string primaryName = FileName(primaryLibraryPath);
	if (!IsSafeLibraryName(primaryName))
	{
		error = "Select an rtlsdr.dll or librtlsdr.dll receiver library.";
		return false;
	}
	const std::string displayName = requestedDisplayName.empty() ?
		(FileName(sourceDirectory).empty() ? FileStem(primaryName) : FileName(sourceDirectory)) : requestedDisplayName;
	const std::string root = receiverRoot.empty() ? GetReceiverRootDirectory() : receiverRoot;
	const std::string customRoot = JoinPath(root, "Custom");
	if (SHCreateDirectoryExA(NULL, customRoot.c_str(), NULL) != ERROR_SUCCESS && !IsDirectory(customRoot))
	{
		error = "PDW could not create the custom receiver folder.";
		return false;
	}
	const std::string baseSlug = Slugify(displayName);
	std::string slug = baseSlug;
	std::string destinationDirectory = JoinPath(customRoot, slug);
	for (unsigned int suffix = 2; IsDirectory(destinationDirectory) && suffix < 1000; ++suffix)
	{
		char suffixText[16];
		snprintf(suffixText, sizeof(suffixText), "-%u", suffix);
		slug = baseSlug + suffixText;
		destinationDirectory = JoinPath(customRoot, slug);
	}
	if (!CreateDirectoryA(destinationDirectory.c_str(), NULL))
	{
		error = "PDW could not create a unique receiver package folder.";
		return false;
	}

	std::uint64_t totalBytes = 0;
	unsigned int copiedFiles = 0;
	WIN32_FIND_DATAA data = {0};
	HANDLE find = FindFirstFileA(JoinPath(sourceDirectory, "*.dll").c_str(), &data);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
		{
			if ((data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
				continue;
			const std::uint64_t size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) |
				data.nFileSizeLow;
			if (size > 128ull * 1024ull * 1024ull || totalBytes + size > 512ull * 1024ull * 1024ull)
			{
				error = "Receiver package exceeds the safe 512 MB import limit.";
				break;
			}
			if (!CopyFileA(JoinPath(sourceDirectory, data.cFileName).c_str(),
				JoinPath(destinationDirectory, data.cFileName).c_str(), TRUE))
			{
				error = "A receiver DLL could not be copied into PDW's receiver folder.";
				break;
			}
			totalBytes += size;
			copiedFiles++;
		} while (FindNextFileA(find, &data));
		FindClose(find);
	}
	if (!error.empty() && error != "Ready.")
	{
		RemoveIncompleteImport(destinationDirectory);
		return false;
	}
	if (copiedFiles == 0)
	{
		RemoveIncompleteImport(destinationDirectory);
		error = "No receiver DLL files were available to import.";
		return false;
	}

	importedPackage.id = "custom-" + slug;
	if (importedPackage.id.size() > 63) importedPackage.id.resize(63);
	importedPackage.displayName = displayName + " (custom)";
	importedPackage.description = std::string("User-imported ") + ReceiverArchitectureLabel() +
		" librtlsdr-compatible receiver package";
	importedPackage.libraryPath = JoinPath(destinationDirectory, primaryName);
	importedPackage.bundled = false;
	if (!WriteReceiverIni(destinationDirectory, importedPackage, primaryName))
	{
		RemoveIncompleteImport(destinationDirectory);
		error = "PDW could not save the imported receiver manifest.";
		return false;
	}
	importedPackage.compatible = ValidateRtlReceiverLibrary(importedPackage.libraryPath,
		importedPackage.status);
	if (!importedPackage.compatible)
	{
		RemoveIncompleteImport(destinationDirectory);
		error = importedPackage.status;
		return false;
	}
	error.clear();
	return true;
}

} // namespace signal
} // namespace pdw
