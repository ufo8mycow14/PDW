#include "headers\message_centre.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "headers\message_archive_manager.h"
#include "headers\pdw.h"
#include "headers\resource.h"
#include "decoded_event.h"

extern PROFILE Profile;
extern TCHAR szPath[MAX_PATH];

namespace
{
	struct CapcodeDialogState
	{
		std::vector<pdw::archive::CapcodeEntry> entries;
		unsigned long selectedColor;
		long long editingId;
		CapcodeDialogState() : selectedColor(RGB(0, 102, 204)), editingId(0) {}
	};

	struct HistoryDialogState
	{
		std::vector<pdw::archive::HistoryRow> rows;
		int offset;
		int total;
		HistoryDialogState() : offset(0), total(0) {}
	};

	std::string ControlText(HWND dialog, int control)
	{
		const int length = GetWindowTextLengthA(GetDlgItem(dialog, control));
		std::vector<char> buffer(static_cast<std::size_t>(length) + 1, 0);
		GetDlgItemTextA(dialog, control, &buffer[0], static_cast<int>(buffer.size()));
		return pdw::events::PdwTextToUtf8(&buffer[0]);
	}

	std::string Utf8ToPdw(const std::string& value)
	{
		if (value.empty()) return std::string();
		const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			value.data(), static_cast<int>(value.size()), NULL, 0);
		if (wideLength <= 0) return value;
		std::vector<wchar_t> wide(static_cast<std::size_t>(wideLength));
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
			static_cast<int>(value.size()), &wide[0], wideLength);
		const int ansiLength = WideCharToMultiByte(CP_ACP, 0, &wide[0], wideLength,
			NULL, 0, NULL, NULL);
		if (ansiLength <= 0) return value;
		std::string result(static_cast<std::size_t>(ansiLength), '\0');
		WideCharToMultiByte(CP_ACP, 0, &wide[0], wideLength, &result[0], ansiLength,
			NULL, NULL);
		return result;
	}

	void SetUtf8Control(HWND dialog, int control, const std::string& value)
	{
		SetDlgItemTextA(dialog, control, Utf8ToPdw(value).c_str());
	}

	void AddListColumn(HWND list, int index, const char* title, int width)
	{
		LVCOLUMNA column = {};
		column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		column.iSubItem = index;
		column.cx = width;
		column.pszText = const_cast<char*>(title);
		ListView_InsertColumn(list, index, &column);
	}

	int InsertListText(HWND list, int row, int column, const std::string& text)
	{
		const std::string display = Utf8ToPdw(text);
		if (column == 0)
		{
			LVITEMA item = {};
			item.mask = LVIF_TEXT | LVIF_PARAM;
			item.iItem = row;
			item.iSubItem = 0;
			item.lParam = row;
			item.pszText = const_cast<char*>(display.c_str());
			return ListView_InsertItem(list, &item);
		}
		ListView_SetItemText(list, row, column, const_cast<char*>(display.c_str()));
		return row;
	}

	void PopulateProtocolCombo(HWND combo, bool includeAll)
	{
		if (includeAll) SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("All protocols"));
		else SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Any protocol"));
		static const char* const protocols[] = { "POCSAG", "FLEX", "ERMES", "ACARS", "MOBITEX" };
		for (std::size_t index = 0; index < sizeof(protocols) / sizeof(protocols[0]); ++index)
			SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(protocols[index]));
		SendMessage(combo, CB_SETCURSEL, 0, 0);
	}

	void PopulateFilterTypeCombo(HWND combo)
	{
		static const char* const names[] = {
			"Auto from protocol", "FLEX capcode", "POCSAG capcode", "Text",
			"ERMES address", "ACARS registration", "MOBITEX MAN"
		};
		for (std::size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
			SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(names[index]));
		SendMessage(combo, CB_SETCURSEL, 0, 0);
	}

	int SelectedFilterType(HWND combo)
	{
		const int selectedType = static_cast<int>(SendMessage(combo, CB_GETCURSEL, 0, 0));
		return selectedType < 0 || selectedType > 6 ? 0 : selectedType;
	}

	void PopulateLabelColorCombo(HWND combo)
	{
		static const char* const colors[] = {
			"1 - Light blue", "2 - Yellow", "3 - Red", "4 - Orange", "5 - Blue",
			"6 - Cyan", "7 - White", "8 - Light green", "9 - Light gray", "10 - Brown",
			"11 - Light cyan", "12 - Navy", "13 - Magenta", "14 - Sea green",
			"15 - Pink", "16 - Ice blue", "17 - Turquoise"
		};
		for (std::size_t index = 0; index < sizeof(colors) / sizeof(colors[0]); ++index)
			SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(colors[index]));
		SendMessage(combo, CB_SETCURSEL, 0, 0);
	}

	void SelectFilterType(HWND combo, int filterType)
	{
		SendMessage(combo, CB_SETCURSEL, filterType >= 0 && filterType <= 6 ? filterType : 0, 0);
	}

	bool Checked(HWND dialog, int control)
	{
		return IsDlgButtonChecked(dialog, control) == BST_CHECKED;
	}

	std::string SelectedProtocol(HWND combo)
	{
		const int selectedIndex = static_cast<int>(SendMessage(combo, CB_GETCURSEL, 0, 0));
		if (selectedIndex <= 0) return std::string();
		char text[32] = {};
		SendMessageA(combo, CB_GETLBTEXT, selectedIndex, reinterpret_cast<LPARAM>(text));
		return text;
	}

	pdw::archive::HistoryQuery HistoryQueryFromDialog(HWND dialog, int limit, int offset)
	{
		pdw::archive::HistoryQuery query;
		query.search = ControlText(dialog, IDC_HISTORY_SEARCH);
		query.protocol = SelectedProtocol(GetDlgItem(dialog, IDC_HISTORY_PROTOCOL));
		query.filteredOnly = IsDlgButtonChecked(dialog, IDC_HISTORY_FILTERED) != 0;
		query.limit = limit;
		query.offset = offset;
		return query;
	}

	void SelectProtocol(HWND combo, const std::string& protocol)
	{
		if (protocol.empty()) { SendMessage(combo, CB_SETCURSEL, 0, 0); return; }
		const int count = static_cast<int>(SendMessage(combo, CB_GETCOUNT, 0, 0));
		for (int index = 1; index < count; ++index)
		{
			char text[32] = {};
			SendMessageA(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text));
			if (_stricmp(text, protocol.c_str()) == 0)
			{
				SendMessage(combo, CB_SETCURSEL, index, 0);
				return;
			}
		}
	}

	void ClearCapcodeEditor(HWND dialog, CapcodeDialogState* state)
	{
		state->editingId = 0;
		SelectProtocol(GetDlgItem(dialog, IDC_CAPCODE_PROTOCOL), std::string());
		SelectFilterType(GetDlgItem(dialog, IDC_CAPCODE_FILTER_TYPE), Profile.filter_default_type);
		SetDlgItemTextA(dialog, IDC_CAPCODE_ADDRESS, "");
		SetDlgItemTextA(dialog, IDC_CAPCODE_NAME, "");
		SetDlgItemTextA(dialog, IDC_CAPCODE_AGENCY, "");
		SetDlgItemTextA(dialog, IDC_CAPCODE_NOTES, "");
		SetDlgItemTextA(dialog, IDC_CAPCODE_FILTER_TEXT, "");
		SetDlgItemTextA(dialog, IDC_CAPCODE_FILTER_LABEL, "");
		SetDlgItemTextA(dialog, IDC_CAPCODE_SEP_FILE1, "");
		SetDlgItemTextA(dialog, IDC_CAPCODE_SEP_FILE2, "");
		SetDlgItemTextA(dialog, IDC_CAPCODE_SEP_FILE3, "");
		CheckDlgButton(dialog, IDC_CAPCODE_ENABLED, BST_CHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_REJECT, BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_MATCH_EXACT, BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_SHOW_LABEL, BST_CHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_COMMAND, BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_MONITOR_ONLY, BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_EMAIL, BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_SEP_ENABLE, BST_UNCHECKED);
		SendDlgItemMessage(dialog, IDC_CAPCODE_LABEL_COLOR, CB_SETCURSEL, 0, 0);
		SetDlgItemTextA(dialog, IDC_CAPCODE_HIT_COUNTER, "Number of hits: 0");
		state->selectedColor = RGB(0, 102, 204);
		SetDlgItemTextA(dialog, IDC_CAPCODE_STATUS,
			"Enter a capcode filter, or choose Text and leave Capcode blank.");
	}

	void RefreshCapcodes(HWND dialog, CapcodeDialogState* state)
	{
		std::string error;
		state->entries.clear();
		HWND list = GetDlgItem(dialog, IDC_CAPCODE_LIST);
		ListView_DeleteAllItems(list);
		if (!MessageArchiveListCapcodes(ControlText(dialog, IDC_CAPCODE_SEARCH), state->entries, error))
		{
			SetUtf8Control(dialog, IDC_CAPCODE_STATUS, error);
			return;
		}
		for (std::size_t index = 0; index < state->entries.size(); ++index)
		{
			const pdw::archive::CapcodeEntry& entry = state->entries[index];
			InsertListText(list, static_cast<int>(index), 0, entry.protocol.empty() ? "Any" : entry.protocol);
			InsertListText(list, static_cast<int>(index), 1, entry.address);
			InsertListText(list, static_cast<int>(index), 2, entry.displayName);
			InsertListText(list, static_cast<int>(index), 3, entry.agency);
			static const char* const types[] = { "Auto", "FLEX", "POCSAG", "Text", "ERMES", "ACARS", "MOBITEX" };
			InsertListText(list, static_cast<int>(index), 4,
				entry.filterType >= 0 && entry.filterType <= 6 ? types[entry.filterType] : "Auto");
			InsertListText(list, static_cast<int>(index), 5, entry.matchText);
			InsertListText(list, static_cast<int>(index), 6,
				entry.filterLabel.empty() ? entry.displayName : entry.filterLabel);
			InsertListText(list, static_cast<int>(index), 7, entry.enabled ? "Yes" : "No");
			char hits[32] = {};
			snprintf(hits, sizeof(hits), "%u", entry.hitCounter);
			InsertListText(list, static_cast<int>(index), 8, hits);
			InsertListText(list, static_cast<int>(index), 9, entry.commandEnabled ? "Yes" : "");
			InsertListText(list, static_cast<int>(index), 10, entry.monitorOnly ? "Yes" : "");
			InsertListText(list, static_cast<int>(index), 11, entry.separateFileEnabled ? "Yes" : "");
			InsertListText(list, static_cast<int>(index), 12, entry.reject ? "Yes" : "");
		}
		std::ostringstream status;
		status << state->entries.size() << " capcode directory entries.";
		SetDlgItemTextA(dialog, IDC_CAPCODE_STATUS, status.str().c_str());
	}

	void LoadSelectedCapcode(HWND dialog, CapcodeDialogState* state)
	{
		const int selectedIndex = ListView_GetNextItem(GetDlgItem(dialog, IDC_CAPCODE_LIST), -1, LVNI_SELECTED);
		if (selectedIndex < 0 || static_cast<std::size_t>(selectedIndex) >= state->entries.size()) return;
		const pdw::archive::CapcodeEntry& entry = state->entries[static_cast<std::size_t>(selectedIndex)];
		state->editingId = entry.id;
		SelectProtocol(GetDlgItem(dialog, IDC_CAPCODE_PROTOCOL), entry.protocol);
		SelectFilterType(GetDlgItem(dialog, IDC_CAPCODE_FILTER_TYPE), entry.filterType);
		SetUtf8Control(dialog, IDC_CAPCODE_ADDRESS, entry.address);
		SetUtf8Control(dialog, IDC_CAPCODE_NAME, entry.displayName);
		SetUtf8Control(dialog, IDC_CAPCODE_AGENCY, entry.agency);
		SetUtf8Control(dialog, IDC_CAPCODE_NOTES, entry.notes);
		SetUtf8Control(dialog, IDC_CAPCODE_FILTER_TEXT, entry.matchText);
		SetUtf8Control(dialog, IDC_CAPCODE_FILTER_LABEL, entry.filterLabel);
		SetUtf8Control(dialog, IDC_CAPCODE_SEP_FILE1, entry.separateFile1);
		SetUtf8Control(dialog, IDC_CAPCODE_SEP_FILE2, entry.separateFile2);
		SetUtf8Control(dialog, IDC_CAPCODE_SEP_FILE3, entry.separateFile3);
		CheckDlgButton(dialog, IDC_CAPCODE_ENABLED, entry.enabled ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_REJECT, entry.reject ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_MATCH_EXACT, entry.matchExactMessage ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_SHOW_LABEL, entry.showFilterLabel ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_COMMAND, entry.commandEnabled ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_MONITOR_ONLY, entry.monitorOnly ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_EMAIL, entry.emailEnabled ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_CAPCODE_SEP_ENABLE, entry.separateFileEnabled ? BST_CHECKED : BST_UNCHECKED);
		SendDlgItemMessage(dialog, IDC_CAPCODE_LABEL_COLOR, CB_SETCURSEL,
			(entry.labelColor >= 0 && entry.labelColor <= 16) ? entry.labelColor : 0, 0);
		std::ostringstream hitText;
		hitText << "Number of hits: " << entry.hitCounter;
		if (!entry.lastHitDate.empty() || !entry.lastHitTime.empty())
			hitText << " - last: " << entry.lastHitDate << ' ' << entry.lastHitTime;
		SetUtf8Control(dialog, IDC_CAPCODE_HIT_COUNTER, hitText.str());
		state->selectedColor = entry.color;
	}

	bool ChooseCsvPath(HWND dialog, bool save, char* path, std::size_t pathSize)
	{
		path[0] = '\0';
		static const char filter[] = "CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0\0";
		OPENFILENAMEA choice = {};
		choice.lStructSize = sizeof(choice);
		choice.hwndOwner = dialog;
		choice.lpstrFilter = filter;
		choice.lpstrFile = path;
		choice.nMaxFile = static_cast<DWORD>(pathSize);
		choice.lpstrDefExt = "csv";
		choice.Flags = OFN_EXPLORER | OFN_HIDEREADONLY |
			(save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
		return (save ? GetSaveFileNameA(&choice) : GetOpenFileNameA(&choice)) != FALSE;
	}

	enum HistoryCsvPathChoice
	{
		HISTORY_CSV_PATH_CANCELLED = 0,
		HISTORY_CSV_PATH_SELECTED = 1,
		HISTORY_CSV_PATH_FAILED = 2
	};

	void BuildDefaultHistoryCsvName(char* path, std::size_t pathSize)
	{
		SYSTEMTIME now = {};
		GetLocalTime(&now);
		_snprintf_s(path, pathSize, _TRUNCATE,
			"PDW-message-history-%04u%02u%02u-%02u%02u%02u.csv",
			static_cast<unsigned int>(now.wYear), static_cast<unsigned int>(now.wMonth),
			static_cast<unsigned int>(now.wDay), static_cast<unsigned int>(now.wHour),
			static_cast<unsigned int>(now.wMinute), static_cast<unsigned int>(now.wSecond));
	}

	HistoryCsvPathChoice ChooseHistoryCsvPath(HWND dialog, char* path,
		std::size_t pathSize, std::string& error)
	{
		error.clear();
		BuildDefaultHistoryCsvName(path, pathSize);
		static const char filter[] = "CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0\0";
		OPENFILENAMEA choice = {};
		choice.lStructSize = sizeof(choice);
		choice.hwndOwner = dialog;
		choice.lpstrFilter = filter;
		choice.nFilterIndex = 1;
		choice.lpstrFile = path;
		choice.nMaxFile = static_cast<DWORD>(pathSize);
		choice.lpstrInitialDir = szPath;
		choice.lpstrTitle = "Export Message History";
		choice.lpstrDefExt = "csv";
		choice.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST |
			OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
		if (GetSaveFileNameA(&choice)) return HISTORY_CSV_PATH_SELECTED;
		const DWORD dialogError = CommDlgExtendedError();
		if (!dialogError) return HISTORY_CSV_PATH_CANCELLED;
		char detail[160] = {};
		snprintf(detail, sizeof(detail),
			"Windows could not open the Save dialog (common-dialog error 0x%08lX).",
			static_cast<unsigned long>(dialogError));
		error = detail;
		return HISTORY_CSV_PATH_FAILED;
	}

	bool NormalizeExportPath(const std::string& path, std::string& normalized)
	{
		char fullPath[MAX_PATH] = {};
		char* filePart = NULL;
		const DWORD fullLength = GetFullPathNameA(path.c_str(),
			static_cast<DWORD>(_countof(fullPath)), fullPath, &filePart);
		if (!fullLength || fullLength >= _countof(fullPath)) return false;

		char longPath[MAX_PATH] = {};
		const DWORD longLength = GetLongPathNameA(fullPath, longPath,
			static_cast<DWORD>(_countof(longPath)));
		if (longLength && longLength < _countof(longPath)) normalized = longPath;
		else if (filePart)
		{
			// The destination normally does not exist yet, so resolve its existing
			// directory separately. This still makes an 8.3 directory alias compare
			// equal to the configured archive and SQLite sidecar paths.
			const std::string directory(fullPath,
				static_cast<std::size_t>(filePart - fullPath));
			char longDirectory[MAX_PATH] = {};
			const DWORD directoryLength = GetLongPathNameA(directory.c_str(),
				longDirectory, static_cast<DWORD>(_countof(longDirectory)));
			if (directoryLength && directoryLength < _countof(longDirectory))
			{
				normalized = longDirectory;
				if (!normalized.empty() && normalized[normalized.size() - 1] != '\\')
					normalized += '\\';
				normalized += filePart;
			}
			else normalized = fullPath;
		}
		else normalized = fullPath;
		std::replace(normalized.begin(), normalized.end(), '/', '\\');
		return true;
	}

	bool IsAbsolutePdwPath(const std::string& path)
	{
		return (path.size() >= 2 && path[1] == ':') ||
			(path.size() >= 2 && path[0] == '\\' && path[1] == '\\');
	}

	bool ValidateHistoryExportDestination(const std::string& chosenPath,
		std::string& destination, std::string& error)
	{
		error.clear();
		if (!NormalizeExportPath(chosenPath, destination))
		{
			error = "The selected CSV path is too long or could not be resolved.";
			return false;
		}

		const std::string configuredArchive = Profile.messageArchivePath;
		if (configuredArchive.empty()) return true;
		std::string archivePath = configuredArchive;
		if (!IsAbsolutePdwPath(archivePath))
		{
			archivePath = szPath;
			if (!archivePath.empty() && archivePath[archivePath.size() - 1] != '\\')
				archivePath += '\\';
			archivePath += configuredArchive;
		}
		std::string normalizedArchive;
		if (!NormalizeExportPath(archivePath, normalizedArchive))
		{
			error = "PDW could not verify that the CSV path is separate from the message archive.";
			return false;
		}

		if (_stricmp(destination.c_str(), normalizedArchive.c_str()) == 0 ||
			_stricmp(destination.c_str(), (normalizedArchive + "-wal").c_str()) == 0 ||
			_stricmp(destination.c_str(), (normalizedArchive + "-shm").c_str()) == 0 ||
			_stricmp(destination.c_str(), (normalizedArchive + "-journal").c_str()) == 0)
		{
			error = "Choose a different file. A CSV export cannot overwrite the message archive database or its SQLite sidecar files.";
			return false;
		}
		return true;
	}

	void SetHistoryExportStatus(HWND dialog, const std::string& status)
	{
		SetUtf8Control(dialog, IDC_HISTORY_STATUS, status);
		HWND statusControl = GetDlgItem(dialog, IDC_HISTORY_STATUS);
		if (statusControl)
			NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, statusControl, OBJID_CLIENT, CHILDID_SELF);
	}

	void ShowHistoryExportError(HWND dialog, const std::string& error)
	{
		SetHistoryExportStatus(dialog, "CSV export failed: " + error);
		const std::string message = Utf8ToPdw(
			"The message history could not be exported.\r\n\r\n" + error);
		MessageBoxA(dialog, message.c_str(), "PDW Message History", MB_OK | MB_ICONERROR);
	}

	bool CreateSiblingTemporaryFile(const std::string& destination,
		char* temporaryPath, std::size_t temporaryPathSize, std::string& error)
	{
		if (temporaryPathSize < MAX_PATH)
		{
			error = "PDW could not prepare a temporary CSV path.";
			return false;
		}
		const std::string::size_type separator = destination.find_last_of("\\/");
		if (separator == std::string::npos)
		{
			error = "The selected CSV folder could not be resolved.";
			return false;
		}
		const std::string directory = destination.substr(0, separator + 1);
		if (!GetTempFileNameA(directory.c_str(), "PDW", 0, temporaryPath))
		{
			char detail[224] = {};
			snprintf(detail, sizeof(detail),
				"PDW could not create a temporary export file in the selected folder (Windows error %lu). Check that the folder is writable.",
				static_cast<unsigned long>(GetLastError()));
			error = detail;
			return false;
		}
		return true;
	}

	bool WriteHistoryCsvAtomically(HWND dialog, const std::string& destination,
		int& exported, std::string& error)
	{
		exported = 0;
		error.clear();
		char temporaryPath[MAX_PATH] = {};
		if (!CreateSiblingTemporaryFile(destination, temporaryPath,
			_countof(temporaryPath), error)) return false;

		std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
		if (!output)
		{
			DeleteFileA(temporaryPath);
			error = "PDW could not open the temporary CSV file. Check that the selected folder is writable.";
			return false;
		}

		const pdw::archive::HistoryQuery query = HistoryQueryFromDialog(dialog, 500, 0);
		if (!MessageArchiveExportHistoryCsv(query, output, exported, error))
		{
			output.close();
			DeleteFileA(temporaryPath);
			if (error.empty()) error = "PDW could not read the matching message history.";
			return false;
		}

		output.flush();
		const bool flushed = output.good();
		output.close();
		if (!flushed || output.fail())
		{
			DeleteFileA(temporaryPath);
			error = "PDW could not finish writing the CSV file. Check available disk space and folder permissions.";
			return false;
		}

		if (!MoveFileExA(temporaryPath, destination.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			const DWORD moveError = GetLastError();
			DeleteFileA(temporaryPath);
			char detail[256] = {};
			snprintf(detail, sizeof(detail),
				"PDW could not replace the selected CSV file (Windows error %lu). The existing file was left unchanged; close it in other programs and try again.",
				static_cast<unsigned long>(moveError));
			error = detail;
			return false;
		}
		return true;
	}

	class HistoryExportUiGuard
	{
	public:
		explicit HistoryExportUiGuard(HWND dialog) : dialog_(dialog),
			button_(GetDlgItem(dialog, IDC_HISTORY_EXPORT)), previousCursor_(NULL)
		{
			EnableWindow(button_, FALSE);
			previousCursor_ = SetCursor(LoadCursor(NULL, IDC_WAIT));
			SetHistoryExportStatus(dialog_, "Exporting all matching messages...");
			UpdateWindow(dialog_);
		}

		~HistoryExportUiGuard()
		{
			EnableWindow(button_, TRUE);
			SetCursor(previousCursor_ ? previousCursor_ : LoadCursor(NULL, IDC_ARROW));
			if (button_) SetFocus(button_);
		}

	private:
		HistoryExportUiGuard(const HistoryExportUiGuard&);
		HistoryExportUiGuard& operator=(const HistoryExportUiGuard&);
		HWND dialog_;
		HWND button_;
		HCURSOR previousCursor_;
	};

	void ExportHistoryCsv(HWND dialog)
	{
		char chosenPath[MAX_PATH] = {};
		std::string error;
		const HistoryCsvPathChoice choice = ChooseHistoryCsvPath(dialog, chosenPath,
			_countof(chosenPath), error);
		if (choice == HISTORY_CSV_PATH_CANCELLED) return;
		if (choice == HISTORY_CSV_PATH_FAILED)
		{
			ShowHistoryExportError(dialog, error);
			return;
		}

		std::string destination;
		if (!ValidateHistoryExportDestination(chosenPath, destination, error))
		{
			ShowHistoryExportError(dialog, error);
			return;
		}

		int exported = 0;
		bool succeeded = false;
		{
			HistoryExportUiGuard progress(dialog);
			succeeded = WriteHistoryCsvAtomically(dialog, destination, exported, error);
		}
		if (!succeeded)
		{
			ShowHistoryExportError(dialog, error);
			return;
		}

		char status[128] = {};
		snprintf(status, sizeof(status),
			"Exported %d matching messages as UTF-8 CSV.", exported);
		SetHistoryExportStatus(dialog, status);
	}

	void ImportCapcodes(HWND dialog, CapcodeDialogState* state)
	{
		char path[MAX_PATH] = {};
		if (!ChooseCsvPath(dialog, false, path, sizeof(path))) return;
		std::ifstream input(path, std::ios::binary);
		if (!input) { MessageBoxA(dialog, "The CSV file could not be opened.", "PDW Capcode Directory", MB_ICONERROR); return; }
		std::vector<pdw::archive::CapcodeEntry> entries;
		int rejected = 0;
		std::string parseError;
		if (!pdw::archive::ReadCapcodeDirectoryCsv(input, entries, rejected, parseError))
		{
			MessageBoxA(dialog, parseError.c_str(), "PDW Capcode Directory", MB_ICONERROR);
			return;
		}
		int imported = 0;
		for (std::vector<pdw::archive::CapcodeEntry>::const_iterator entry = entries.begin();
			entry != entries.end(); ++entry)
		{
			std::string importError;
			if (MessageArchiveUpsertCapcode(*entry, importError)) ++imported; else ++rejected;
		}
		std::string reloadError;
		if (!MessageArchiveReloadRuntimeFilters(reloadError))
			MessageBoxA(dialog, reloadError.c_str(), "PDW Capcode Directory", MB_ICONERROR);
		RefreshCapcodes(dialog, state);
		std::ostringstream status;
		status << "Imported " << imported << " entries";
		if (rejected) status << "; rejected " << rejected << " invalid rows";
		status << '.';
		SetDlgItemTextA(dialog, IDC_CAPCODE_STATUS, status.str().c_str());
	}

	void ExportCapcodes(HWND dialog, CapcodeDialogState* state)
	{
		char path[MAX_PATH] = {};
		if (!ChooseCsvPath(dialog, true, path, sizeof(path))) return;
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output) { MessageBoxA(dialog, "The CSV file could not be written.", "PDW Capcode Directory", MB_ICONERROR); return; }
		std::string error;
		if (!pdw::archive::WriteCapcodeDirectoryCsv(state->entries, output, error))
		{
			MessageBoxA(dialog, error.c_str(), "PDW Capcode Directory", MB_ICONERROR);
			return;
		}
		SetDlgItemTextA(dialog, IDC_CAPCODE_STATUS, "Capcode directory exported as UTF-8 CSV.");
	}

	void InitializeHistoryList(HWND list)
	{
		ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
		AddListColumn(list, 0, "Received", 135);
		AddListColumn(list, 1, "Protocol", 78);
		AddListColumn(list, 2, "Capcode", 78);
		AddListColumn(list, 3, "Name", 120);
		AddListColumn(list, 4, "Agency", 120);
		AddListColumn(list, 5, "Type", 76);
		AddListColumn(list, 6, "Message", 300);
		AddListColumn(list, 7, "Filter", 110);
	}

	void RefreshHistory(HWND dialog, HistoryDialogState* state, bool resetPage)
	{
		if (resetPage) state->offset = 0;
		const pdw::archive::HistoryQuery query =
			HistoryQueryFromDialog(dialog, 200, state->offset);
		std::string error;
		state->rows.clear();
		if (!MessageArchiveQueryHistory(query, state->rows, state->total, error))
		{
			SetUtf8Control(dialog, IDC_HISTORY_STATUS, error);
			return;
		}
		HWND list = GetDlgItem(dialog, IDC_HISTORY_LIST);
		ListView_DeleteAllItems(list);
		for (std::size_t index = 0; index < state->rows.size(); ++index)
		{
			const pdw::archive::HistoryRow& row = state->rows[index];
			InsertListText(list, static_cast<int>(index), 0, row.event.timestamp);
			InsertListText(list, static_cast<int>(index), 1, row.event.mode);
			InsertListText(list, static_cast<int>(index), 2, row.event.address);
			InsertListText(list, static_cast<int>(index), 3, row.displayName);
			InsertListText(list, static_cast<int>(index), 4, row.agency);
			InsertListText(list, static_cast<int>(index), 5, row.event.messageType);
			InsertListText(list, static_cast<int>(index), 6, row.event.message);
			InsertListText(list, static_cast<int>(index), 7, row.event.filterLabel);
		}
		const int page = state->offset / 200 + 1;
		const int pages = (std::max)(1, (state->total + 199) / 200);
		char pageText[128];
		snprintf(pageText, sizeof(pageText), "Page %d of %d - %d matching messages", page, pages, state->total);
		SetDlgItemTextA(dialog, IDC_HISTORY_PAGE, pageText);
		EnableWindow(GetDlgItem(dialog, IDC_HISTORY_PREVIOUS), state->offset > 0);
		EnableWindow(GetDlgItem(dialog, IDC_HISTORY_NEXT), state->offset + 200 < state->total);
		SetDlgItemTextA(dialog, IDC_HISTORY_STATUS, MessageArchiveStatusText().c_str());
	}

	bool SaveHistorySettings(HWND dialog)
	{
		BOOL valid = FALSE;
		const UINT retention = GetDlgItemInt(dialog, IDC_HISTORY_RETENTION, &valid, FALSE);
		if (!valid || retention < 1 || retention > 3650)
		{
			MessageBoxA(dialog, "Retention must be between 1 and 3650 days.", "PDW Message History", MB_ICONERROR);
			return false;
		}
		const std::string utf8Path = ControlText(dialog, IDC_HISTORY_PATH);
		if (utf8Path.empty())
		{
			MessageBoxA(dialog, "Choose a database path.", "PDW Message History", MB_ICONERROR);
			return false;
		}
		const std::string path = Utf8ToPdw(utf8Path);
		const std::string previousPath = Profile.messageArchivePath;
		const int previousHistoryEnabled = Profile.messageHistoryEnabled;
		const int previousIncludeMessage = Profile.messageHistoryIncludeMessage;
		const unsigned int previousRetentionDays = Profile.messageHistoryRetentionDays;
		const int previousDashboardEnabled = Profile.liveDashboardEnabled;
		strncpy(Profile.messageArchivePath, path.c_str(), sizeof(Profile.messageArchivePath) - 1);
		Profile.messageArchivePath[sizeof(Profile.messageArchivePath) - 1] = '\0';
		Profile.messageHistoryEnabled = IsDlgButtonChecked(dialog, IDC_HISTORY_ENABLE) != 0;
		Profile.messageHistoryIncludeMessage = IsDlgButtonChecked(dialog, IDC_HISTORY_MESSAGE) != 0;
		Profile.messageHistoryRetentionDays = retention;
		if (!Profile.messageHistoryEnabled) Profile.liveDashboardEnabled = 0;
		if (!TryWriteSettings())
		{
			strncpy(Profile.messageArchivePath, previousPath.c_str(),
				sizeof(Profile.messageArchivePath) - 1);
			Profile.messageArchivePath[sizeof(Profile.messageArchivePath) - 1] = '\0';
			Profile.messageHistoryEnabled = previousHistoryEnabled;
			Profile.messageHistoryIncludeMessage = previousIncludeMessage;
			Profile.messageHistoryRetentionDays = previousRetentionDays;
			Profile.liveDashboardEnabled = previousDashboardEnabled;
			SetDlgItemTextA(dialog, IDC_HISTORY_STATUS,
				"Message History settings were not saved; the previous configuration was restored.");
			MessageBoxA(dialog,
				"PDW could not save the Message History settings. The previous history configuration was restored. Choose a writable portable folder or correct PDW.INI permissions, then try again.",
				"PDW Message History", MB_OK | MB_ICONERROR);
			return false;
		}
		MessageArchiveSettingsChanged();
		SetDlgItemTextA(dialog, IDC_HISTORY_STATUS, MessageArchiveStatusText().c_str());
		return true;
	}

	void BrowseHistoryPath(HWND dialog)
	{
		char path[MESSAGE_ARCHIVE_PATH_LEN + 1] = {};
		GetDlgItemTextA(dialog, IDC_HISTORY_PATH, path, sizeof(path));
		static const char filter[] = "SQLite databases (*.sqlite3;*.sqlite;*.db)\0*.sqlite3;*.sqlite;*.db\0All files (*.*)\0*.*\0\0";
		OPENFILENAMEA choice = {};
		choice.lStructSize = sizeof(choice);
		choice.hwndOwner = dialog;
		choice.lpstrFilter = filter;
		choice.lpstrFile = path;
		choice.nMaxFile = sizeof(path);
		choice.lpstrDefExt = "sqlite3";
		choice.Flags = OFN_EXPLORER | OFN_HIDEREADONLY;
		if (GetSaveFileNameA(&choice)) SetDlgItemTextA(dialog, IDC_HISTORY_PATH, path);
	}

	void InitializeDirectoryOptions(HWND dialog)
	{
		CheckDlgButton(dialog, IDC_FILTERFILEEN, Profile.filterfile_enabled ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_FILTERFILEDATE, Profile.filterfile_use_date ? BST_CHECKED : BST_UNCHECKED);
		SetDlgItemTextA(dialog, IDC_FILTERFILE, Profile.filterfile);
		CheckDlgButton(dialog, IDC_LABELLOG, Profile.LabelLog ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_LABELNEWLINE, Profile.LabelNewline ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_FILTERCMDEN, Profile.filter_cmd_file_enabled ? BST_CHECKED : BST_UNCHECKED);
		SetDlgItemTextA(dialog, IDC_FILTERCMDFILE, Profile.filter_cmd);
		SetDlgItemTextA(dialog, IDC_FILTERCMDARGS, Profile.filter_cmd_args);
		CheckDlgButton(dialog, IDC_FILTERTONE, Profile.showtone ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_FILTERNUMERIC, Profile.shownumeric ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_FILTERBINARYHEX, Profile.showmisc ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_FILTERSCOLORS, Profile.FilterWindowColors ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_FILTERWINDOWONLY, Profile.filterwindowonly ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_FILTERBEEP, Profile.filterbeep ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(dialog, IDC_FILTERSEXTRA, Profile.FilterWindowExtra ? BST_CHECKED : BST_UNCHECKED);
		PopulateFilterTypeCombo(GetDlgItem(dialog, IDC_FILTERDEFTYPE));
		SelectFilterType(GetDlgItem(dialog, IDC_FILTERDEFTYPE), Profile.filter_default_type);
		for (int column = 1; column <= 7; ++column)
			CheckDlgButton(dialog, IDC_FILTERLOGCOLUMN + column,
				strchr(Profile.ColFilterfile, '0' + column) ? BST_CHECKED : BST_UNCHECKED);
	}

	void UpdateDirectoryOptionEnables(HWND dialog)
	{
		const BOOL fileEnabled = Checked(dialog, IDC_FILTERFILEEN) ? TRUE : FALSE;
		EnableWindow(GetDlgItem(dialog, IDC_FILTERFILE), fileEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_FILTERBROWSE), fileEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_FILTERFILEDATE), fileEnabled);
		const BOOL commandEnabled = Checked(dialog, IDC_FILTERCMDEN) ? TRUE : FALSE;
		EnableWindow(GetDlgItem(dialog, IDC_FILTERCMDFILE), commandEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_FILTERCMDBROWSE), commandEnabled);
		EnableWindow(GetDlgItem(dialog, IDC_FILTERCMDARGS), commandEnabled);
	}

	void UpdateDirectoryExtraColumns(HWND dialog)
	{
		HWND list = GetDlgItem(dialog, IDC_CAPCODE_LIST);
		const bool show = Checked(dialog, IDC_FILTERSEXTRA);
		const int widths[4] = { 46, 46, 46, 52 };
		for (int column = 9; column <= 12; ++column)
			ListView_SetColumnWidth(list, column, show ? widths[column - 9] : 0);
	}

	bool SaveDirectoryOptions(HWND dialog)
	{
		char selectedColumns[10] = {};
		for (int column = 1; column <= 7; ++column)
		{
			if (!Checked(dialog, IDC_FILTERLOGCOLUMN + column)) continue;
			const std::size_t length = strlen(selectedColumns);
			selectedColumns[length] = static_cast<char>('0' + column);
			selectedColumns[length + 1] = '\0';
		}
		if (!selectedColumns[0])
		{
			MessageBoxA(dialog, "Select at least one column for filter and separate CSV files.",
				"PDW Capcode Directory", MB_OK | MB_ICONWARNING);
			return false;
		}
		if (Checked(dialog, IDC_FILTERFILEEN) && !Checked(dialog, IDC_FILTERFILEDATE) &&
			ControlText(dialog, IDC_FILTERFILE).empty())
		{
			MessageBoxA(dialog, "Choose a filter CSV file, or enable date-based filenames.",
				"PDW Capcode Directory", MB_OK | MB_ICONWARNING);
			return false;
		}
		if (Checked(dialog, IDC_FILTERCMDEN) && ControlText(dialog, IDC_FILTERCMDFILE).empty())
		{
			MessageBoxA(dialog, "Choose the command file before enabling filter commands.",
				"PDW Capcode Directory", MB_OK | MB_ICONWARNING);
			return false;
		}
		Profile.filterfile_enabled = Checked(dialog, IDC_FILTERFILEEN) ? 1 : 0;
		Profile.filterfile_use_date = Checked(dialog, IDC_FILTERFILEDATE) ? 1 : 0;
		const std::string filterFile = Utf8ToPdw(ControlText(dialog, IDC_FILTERFILE));
		strncpy(Profile.filterfile, filterFile.c_str(), sizeof(Profile.filterfile) - 1);
		Profile.filterfile[sizeof(Profile.filterfile) - 1] = '\0';
		Profile.LabelLog = Checked(dialog, IDC_LABELLOG) ? 1 : 0;
		Profile.LabelNewline = Checked(dialog, IDC_LABELNEWLINE) ? 1 : 0;
		Profile.filter_cmd_file_enabled = Checked(dialog, IDC_FILTERCMDEN) ? 1 : 0;
		const std::string commandFile = Utf8ToPdw(ControlText(dialog, IDC_FILTERCMDFILE));
		const std::string commandArgs = Utf8ToPdw(ControlText(dialog, IDC_FILTERCMDARGS));
		strncpy(Profile.filter_cmd, commandFile.c_str(), sizeof(Profile.filter_cmd) - 1);
		Profile.filter_cmd[sizeof(Profile.filter_cmd) - 1] = '\0';
		strncpy(Profile.filter_cmd_args, commandArgs.c_str(), sizeof(Profile.filter_cmd_args) - 1);
		Profile.filter_cmd_args[sizeof(Profile.filter_cmd_args) - 1] = '\0';
		Profile.showtone = Checked(dialog, IDC_FILTERTONE) ? 1u : 0u;
		Profile.shownumeric = Checked(dialog, IDC_FILTERNUMERIC) ? 1u : 0u;
		Profile.showmisc = Checked(dialog, IDC_FILTERBINARYHEX) ? 1u : 0u;
		Profile.FilterWindowColors = Checked(dialog, IDC_FILTERSCOLORS) ? 1 : 0;
		Profile.filterwindowonly = Checked(dialog, IDC_FILTERWINDOWONLY) ? 1u : 0u;
		Profile.FilterWindowExtra = Checked(dialog, IDC_FILTERSEXTRA) ? 1 : 0;
		Profile.filter_default_type = SelectedFilterType(GetDlgItem(dialog, IDC_FILTERDEFTYPE));
		strcpy(Profile.ColFilterfile, selectedColumns);
		WriteSettings();
		UpdateDirectoryExtraColumns(dialog);
		SetDlgItemTextA(dialog, IDC_CAPCODE_STATUS, "Directory and filter options saved.");
		return true;
	}

	bool ChooseOutputCsv(HWND dialog, int control)
	{
		char path[MAX_PATH] = {};
		if (!ChooseCsvPath(dialog, true, path, sizeof(path))) return false;
		SetDlgItemTextA(dialog, control, path);
		return true;
	}

	void ChooseCommandFile(HWND dialog)
	{
		char path[MAX_PATH] = {};
		static const char filter[] = "Programs and scripts (*.exe;*.com;*.bat;*.cmd)\0*.exe;*.com;*.bat;*.cmd\0All files (*.*)\0*.*\0\0";
		OPENFILENAMEA choice = {};
		choice.lStructSize = sizeof(choice);
		choice.hwndOwner = dialog;
		choice.lpstrFilter = filter;
		choice.lpstrFile = path;
		choice.nMaxFile = sizeof(path);
		choice.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
		if (GetOpenFileNameA(&choice)) SetDlgItemTextA(dialog, IDC_FILTERCMDFILE, path);
	}

	pdw::archive::CapcodeEntry EntryFromDialog(HWND dialog, CapcodeDialogState* state)
	{
		pdw::archive::CapcodeEntry entry;
		for (std::vector<pdw::archive::CapcodeEntry>::const_iterator existing = state->entries.begin();
			existing != state->entries.end(); ++existing)
		{
			if (existing->id == state->editingId) { entry = *existing; break; }
		}
		entry.id = state->editingId;
		entry.protocol = SelectedProtocol(GetDlgItem(dialog, IDC_CAPCODE_PROTOCOL));
		entry.filterType = SelectedFilterType(GetDlgItem(dialog, IDC_CAPCODE_FILTER_TYPE));
		entry.address = ControlText(dialog, IDC_CAPCODE_ADDRESS);
		entry.displayName = ControlText(dialog, IDC_CAPCODE_NAME);
		entry.agency = ControlText(dialog, IDC_CAPCODE_AGENCY);
		entry.notes = ControlText(dialog, IDC_CAPCODE_NOTES);
		entry.matchText = ControlText(dialog, IDC_CAPCODE_FILTER_TEXT);
		entry.filterLabel = ControlText(dialog, IDC_CAPCODE_FILTER_LABEL);
		entry.enabled = Checked(dialog, IDC_CAPCODE_ENABLED);
		entry.reject = Checked(dialog, IDC_CAPCODE_REJECT);
		entry.matchExactMessage = Checked(dialog, IDC_CAPCODE_MATCH_EXACT);
		entry.showFilterLabel = Checked(dialog, IDC_CAPCODE_SHOW_LABEL);
		entry.commandEnabled = Checked(dialog, IDC_CAPCODE_COMMAND);
		entry.monitorOnly = Checked(dialog, IDC_CAPCODE_MONITOR_ONLY);
		entry.emailEnabled = Checked(dialog, IDC_CAPCODE_EMAIL);
		entry.separateFileEnabled = Checked(dialog, IDC_CAPCODE_SEP_ENABLE);
		entry.separateFile1 = ControlText(dialog, IDC_CAPCODE_SEP_FILE1);
		entry.separateFile2 = ControlText(dialog, IDC_CAPCODE_SEP_FILE2);
		entry.separateFile3 = ControlText(dialog, IDC_CAPCODE_SEP_FILE3);
		const int labelColor = static_cast<int>(SendDlgItemMessage(dialog,
			IDC_CAPCODE_LABEL_COLOR, CB_GETCURSEL, 0, 0));
		entry.labelColor = labelColor >= 0 && labelColor <= 16 ? labelColor : 0;
		entry.color = state->selectedColor;
		return entry;
	}

	bool ReloadDirectoryRuntime(HWND dialog)
	{
		std::string error;
		if (MessageArchiveReloadRuntimeFilters(error)) return true;
		SetUtf8Control(dialog, IDC_CAPCODE_STATUS, error);
		MessageBoxA(dialog, Utf8ToPdw(error).c_str(), "PDW Capcode Directory", MB_ICONERROR);
		return false;
	}
}

BOOL FAR PASCAL CapcodeDirectoryDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CapcodeDialogState* state = reinterpret_cast<CapcodeDialogState*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			state = new CapcodeDialogState();
			SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
			CenterWindow(hDlg);
			HWND list = GetDlgItem(hDlg, IDC_CAPCODE_LIST);
			ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
			AddListColumn(list, 0, "Protocol", 80);
			AddListColumn(list, 1, "Capcode", 85);
			AddListColumn(list, 2, "Display name", 150);
			AddListColumn(list, 3, "Agency / service", 130);
			AddListColumn(list, 4, "Filter type", 85);
			AddListColumn(list, 5, "Message text", 150);
			AddListColumn(list, 6, "Label", 150);
			AddListColumn(list, 7, "Enabled", 60);
			AddListColumn(list, 8, "Hits", 55);
			AddListColumn(list, 9, "CMD", 46);
			AddListColumn(list, 10, "MON", 46);
			AddListColumn(list, 11, "SEP", 46);
			AddListColumn(list, 12, "Reject", 52);
			PopulateProtocolCombo(GetDlgItem(hDlg, IDC_CAPCODE_PROTOCOL), false);
			PopulateFilterTypeCombo(GetDlgItem(hDlg, IDC_CAPCODE_FILTER_TYPE));
			PopulateLabelColorCombo(GetDlgItem(hDlg, IDC_CAPCODE_LABEL_COLOR));
			SendDlgItemMessage(hDlg, IDC_CAPCODE_ADDRESS, EM_LIMITTEXT, 18, 0);
			SendDlgItemMessage(hDlg, IDC_CAPCODE_NAME, EM_LIMITTEXT, FILTER_LABEL_LEN, 0);
			SendDlgItemMessage(hDlg, IDC_CAPCODE_FILTER_TEXT, EM_LIMITTEXT, FILTER_TEXT_LEN, 0);
			SendDlgItemMessage(hDlg, IDC_CAPCODE_FILTER_LABEL, EM_LIMITTEXT, FILTER_LABEL_LEN, 0);
			SendDlgItemMessage(hDlg, IDC_CAPCODE_SEP_FILE1, EM_LIMITTEXT, FILTER_FILE_LEN, 0);
			SendDlgItemMessage(hDlg, IDC_CAPCODE_SEP_FILE2, EM_LIMITTEXT, FILTER_FILE_LEN, 0);
			SendDlgItemMessage(hDlg, IDC_CAPCODE_SEP_FILE3, EM_LIMITTEXT, FILTER_FILE_LEN, 0);
			SendDlgItemMessage(hDlg, IDC_FILTERFILE, EM_LIMITTEXT, MAX_FILE_LEN, 0);
			SendDlgItemMessage(hDlg, IDC_FILTERCMDFILE, EM_LIMITTEXT, MAX_FILE_LEN, 0);
			SendDlgItemMessage(hDlg, IDC_FILTERCMDARGS, EM_LIMITTEXT, MAX_FILE_LEN, 0);
			InitializeDirectoryOptions(hDlg);
			UpdateDirectoryOptionEnables(hDlg);
			UpdateDirectoryExtraColumns(hDlg);
			ClearCapcodeEditor(hDlg, state);
			RefreshCapcodes(hDlg, state);
			return TRUE;
		}

		case WM_NOTIFY:
			if (state && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_CAPCODE_LIST)
			{
				const int code = reinterpret_cast<NMHDR*>(lParam)->code;
				if (code == LVN_ITEMCHANGED) LoadSelectedCapcode(hDlg, state);
				else if (code == NM_CUSTOMDRAW && Profile.FilterWindowColors)
				{
					NMLVCUSTOMDRAW* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
					if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
					{
						SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
						return TRUE;
					}
					if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT &&
						draw->nmcd.dwItemSpec < state->entries.size())
					{
						const int colorIndex = state->entries[draw->nmcd.dwItemSpec].labelColor;
						draw->clrText = Profile.color_filterlabel[
							colorIndex >= 0 && colorIndex <= 16 ? colorIndex : 0];
						SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_DODEFAULT);
						return TRUE;
					}
				}
			}
			return TRUE;

		case WM_COMMAND:
			if (!state) break;
			switch (LOWORD(wParam))
			{
				case IDC_CAPCODE_SEARCH:
					if (HIWORD(wParam) == EN_CHANGE) RefreshCapcodes(hDlg, state);
					return TRUE;
				case IDC_CAPCODE_NEW:
					ClearCapcodeEditor(hDlg, state); return TRUE;
				case IDC_FILTERRELOAD:
					if (ReloadDirectoryRuntime(hDlg))
					{
						RefreshCapcodes(hDlg, state);
						SetDlgItemTextA(hDlg, IDC_CAPCODE_STATUS, "Capcode Directory reloaded; live filters are current.");
					}
					return TRUE;
				case IDC_FILTERFILEEN:
				case IDC_FILTERCMDEN:
					UpdateDirectoryOptionEnables(hDlg); return TRUE;
				case IDC_FILTERBROWSE:
					ChooseOutputCsv(hDlg, IDC_FILTERFILE); return TRUE;
				case IDC_FILTERCMDBROWSE:
					ChooseCommandFile(hDlg); return TRUE;
				case IDC_CAPCODE_SEP_BROWSE:
					{
						int target = IDC_CAPCODE_SEP_FILE1;
						if (!ControlText(hDlg, target).empty()) target = IDC_CAPCODE_SEP_FILE2;
						if (!ControlText(hDlg, target).empty()) target = IDC_CAPCODE_SEP_FILE3;
						ChooseOutputCsv(hDlg, target);
						return TRUE;
					}
				case IDC_FILTER_APPLY:
					SaveDirectoryOptions(hDlg); UpdateDirectoryOptionEnables(hDlg); return TRUE;
				case IDC_CAPCODE_COLOR:
				{
					static COLORREF custom[16] = {};
					CHOOSECOLORA choice = {};
					choice.lStructSize = sizeof(choice); choice.hwndOwner = hDlg;
					choice.rgbResult = state->selectedColor; choice.lpCustColors = custom;
					choice.Flags = CC_FULLOPEN | CC_RGBINIT;
					if (ChooseColorA(&choice)) state->selectedColor = choice.rgbResult;
					return TRUE;
				}
				case IDC_CAPCODE_SAVE:
				{
					pdw::archive::CapcodeEntry entry = EntryFromDialog(hDlg, state);
					if (entry.separateFileEnabled && entry.separateFile1.empty() &&
						entry.separateFile2.empty() && entry.separateFile3.empty())
					{
						MessageBoxA(hDlg, "Choose at least one separate filter CSV file.",
							"PDW Capcode Directory", MB_OK | MB_ICONWARNING);
						return TRUE;
					}
					std::string error;
					if (!MessageArchiveUpsertCapcode(entry, error)) SetUtf8Control(hDlg, IDC_CAPCODE_STATUS, error);
					else if (ReloadDirectoryRuntime(hDlg))
					{
						RefreshCapcodes(hDlg, state);
						SetDlgItemTextA(hDlg, IDC_CAPCODE_STATUS,
							"Saved and applied immediately to monitor, filtered pane, logs and outputs.");
					}
					return TRUE;
				}
				case IDC_CAPCODE_HIT_RESET:
				{
					if (state->editingId <= 0) return TRUE;
					pdw::archive::CapcodeEntry entry = EntryFromDialog(hDlg, state);
					entry.hitCounter = 0;
					entry.lastHitDate.clear();
					entry.lastHitTime.clear();
					std::string error;
					if (!MessageArchiveUpsertCapcode(entry, error) || !ReloadDirectoryRuntime(hDlg))
					{
						if (!error.empty()) SetUtf8Control(hDlg, IDC_CAPCODE_STATUS, error);
						return TRUE;
					}
					RefreshCapcodes(hDlg, state);
					SetDlgItemTextA(hDlg, IDC_CAPCODE_HIT_COUNTER, "Number of hits: 0");
					SetDlgItemTextA(hDlg, IDC_CAPCODE_STATUS, "Hit counter reset.");
					return TRUE;
				}
				case IDC_CAPCODE_DELETE:
				{
					const int selectedIndex = ListView_GetNextItem(GetDlgItem(hDlg, IDC_CAPCODE_LIST), -1, LVNI_SELECTED);
					if (selectedIndex < 0 || static_cast<std::size_t>(selectedIndex) >= state->entries.size()) return TRUE;
					if (MessageBoxA(hDlg, "Delete the selected capcode directory entry? Message history will remain intact.",
						"PDW Capcode Directory", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return TRUE;
					std::string error;
					const pdw::archive::CapcodeEntry entry = state->entries[static_cast<std::size_t>(selectedIndex)];
					if (!MessageArchiveDeleteCapcode(entry.id, error)) SetUtf8Control(hDlg, IDC_CAPCODE_STATUS, error);
					else if (ReloadDirectoryRuntime(hDlg)) { ClearCapcodeEditor(hDlg, state); RefreshCapcodes(hDlg, state); }
					return TRUE;
				}
				case IDC_CAPCODE_IMPORT: ImportCapcodes(hDlg, state); return TRUE;
				case IDC_CAPCODE_EXPORT: ExportCapcodes(hDlg, state); return TRUE;
				case IDOK:
				case IDCANCEL:
					EndDialog(hDlg, TRUE); return TRUE;
			}
			break;

		case WM_DESTROY:
			delete state;
			SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
			return TRUE;
	}
	return FALSE;
}

BOOL FAR PASCAL MessageHistoryDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
	HistoryDialogState* state = reinterpret_cast<HistoryDialogState*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
	switch (uMsg)
	{
		case WM_INITDIALOG:
			state = new HistoryDialogState();
			SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
			CenterWindow(hDlg);
			InitializeHistoryList(GetDlgItem(hDlg, IDC_HISTORY_LIST));
			PopulateProtocolCombo(GetDlgItem(hDlg, IDC_HISTORY_PROTOCOL), true);
			CheckDlgButton(hDlg, IDC_HISTORY_ENABLE, Profile.messageHistoryEnabled ? BST_CHECKED : BST_UNCHECKED);
			CheckDlgButton(hDlg, IDC_HISTORY_MESSAGE, Profile.messageHistoryIncludeMessage ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemInt(hDlg, IDC_HISTORY_RETENTION, Profile.messageHistoryRetentionDays, FALSE);
			SetDlgItemTextA(hDlg, IDC_HISTORY_PATH, Profile.messageArchivePath);
			RefreshHistory(hDlg, state, true);
			return TRUE;

		case WM_COMMAND:
			if (!state) break;
			switch (LOWORD(wParam))
			{
				case IDC_HISTORY_BROWSE: BrowseHistoryPath(hDlg); return TRUE;
				case IDC_HISTORY_SAVE_SETTINGS:
					if (SaveHistorySettings(hDlg)) RefreshHistory(hDlg, state, true);
					return TRUE;
				case IDC_HISTORY_REFRESH: RefreshHistory(hDlg, state, true); return TRUE;
				case IDC_HISTORY_EXPORT: ExportHistoryCsv(hDlg); return TRUE;
				case IDC_HISTORY_PREVIOUS:
					state->offset = (std::max)(0, state->offset - 200); RefreshHistory(hDlg, state, false); return TRUE;
				case IDC_HISTORY_NEXT:
					if (state->offset + 200 < state->total) state->offset += 200;
					RefreshHistory(hDlg, state, false); return TRUE;
				case IDC_HISTORY_PURGE:
				{
					if (MessageBoxA(hDlg, "Delete message-history rows older than the configured retention period? Capcode directory entries are not deleted.",
						"PDW Message History", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return TRUE;
					BOOL valid = FALSE; const UINT days = GetDlgItemInt(hDlg, IDC_HISTORY_RETENTION, &valid, FALSE);
					int removed = 0; std::string error;
					if (!valid || !MessageArchivePurgeHistory(days, removed, error)) SetUtf8Control(hDlg, IDC_HISTORY_STATUS, valid ? error : "Enter a valid retention period.");
					else { char status[128]; snprintf(status, sizeof(status), "Removed %d expired message-history rows.", removed); SetDlgItemTextA(hDlg, IDC_HISTORY_STATUS, status); RefreshHistory(hDlg, state, true); }
					return TRUE;
				}
				case IDOK:
				case IDCANCEL: EndDialog(hDlg, TRUE); return TRUE;
			}
			break;

		case WM_DESTROY:
			delete state; SetWindowLongPtr(hDlg, GWLP_USERDATA, 0); return TRUE;
	}
	return FALSE;
}

BOOL FAR PASCAL LiveDashboardDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			CenterWindow(hDlg);
			CheckDlgButton(hDlg, IDC_DASHBOARD_ENABLE, Profile.liveDashboardEnabled ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemInt(hDlg, IDC_DASHBOARD_PORT, Profile.liveDashboardPort, FALSE);
			SetDlgItemTextA(hDlg, IDC_DASHBOARD_STATUS, MessageArchiveStatusText().c_str());
			return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_DASHBOARD_SAVE:
				{
					BOOL valid = FALSE;
					const UINT port = GetDlgItemInt(hDlg, IDC_DASHBOARD_PORT, &valid, FALSE);
					const bool enabled = IsDlgButtonChecked(hDlg, IDC_DASHBOARD_ENABLE) != 0;
					if (!valid || port < 1 || port > 65535)
					{
						MessageBoxA(hDlg, "Port must be between 1 and 65535.", "PDW Local Dashboard", MB_ICONERROR); return TRUE;
					}
					if (enabled && !Profile.messageHistoryEnabled)
					{
						MessageBoxA(hDlg, "Enable local Message History first. The dashboard reads from that bounded local archive.", "PDW Local Dashboard", MB_ICONINFORMATION); return TRUE;
					}
					Profile.liveDashboardEnabled = enabled ? 1 : 0;
					Profile.liveDashboardPort = port;
					WriteSettings();
					MessageArchiveSettingsChanged();
					SetDlgItemTextA(hDlg, IDC_DASHBOARD_STATUS, MessageArchiveStatusText().c_str());
					return TRUE;
				}
				case IDC_DASHBOARD_OPEN:
				{
					char url[128]; snprintf(url, sizeof(url), "http://127.0.0.1:%d/", Profile.liveDashboardPort);
					ShellExecuteA(hDlg, "open", url, NULL, NULL, SW_SHOWNORMAL); return TRUE;
				}
				case IDOK:
				case IDCANCEL: EndDialog(hDlg, TRUE); return TRUE;
			}
			break;
	}
	return FALSE;
}
