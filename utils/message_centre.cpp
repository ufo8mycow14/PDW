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

namespace
{
	struct CapcodeDialogState
	{
		std::vector<pdw::archive::CapcodeEntry> entries;
		unsigned long selectedColor;
		CapcodeDialogState() : selectedColor(RGB(0, 102, 204)) {}
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

	std::string SelectedProtocol(HWND combo)
	{
		const int selectedIndex = static_cast<int>(SendMessage(combo, CB_GETCURSEL, 0, 0));
		if (selectedIndex <= 0) return std::string();
		char text[32] = {};
		SendMessageA(combo, CB_GETLBTEXT, selectedIndex, reinterpret_cast<LPARAM>(text));
		return text;
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
		SelectProtocol(GetDlgItem(dialog, IDC_CAPCODE_PROTOCOL), std::string());
		SetDlgItemTextA(dialog, IDC_CAPCODE_ADDRESS, "");
		SetDlgItemTextA(dialog, IDC_CAPCODE_NAME, "");
		SetDlgItemTextA(dialog, IDC_CAPCODE_AGENCY, "");
		SetDlgItemTextA(dialog, IDC_CAPCODE_NOTES, "");
		CheckDlgButton(dialog, IDC_CAPCODE_ENABLED, BST_CHECKED);
		state->selectedColor = RGB(0, 102, 204);
		SetDlgItemTextA(dialog, IDC_CAPCODE_STATUS, "Enter a capcode and its additional display information.");
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
			InsertListText(list, static_cast<int>(index), 4, entry.enabled ? "Yes" : "No");
			InsertListText(list, static_cast<int>(index), 5, entry.notes);
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
		SelectProtocol(GetDlgItem(dialog, IDC_CAPCODE_PROTOCOL), entry.protocol);
		SetUtf8Control(dialog, IDC_CAPCODE_ADDRESS, entry.address);
		SetUtf8Control(dialog, IDC_CAPCODE_NAME, entry.displayName);
		SetUtf8Control(dialog, IDC_CAPCODE_AGENCY, entry.agency);
		SetUtf8Control(dialog, IDC_CAPCODE_NOTES, entry.notes);
		CheckDlgButton(dialog, IDC_CAPCODE_ENABLED, entry.enabled ? BST_CHECKED : BST_UNCHECKED);
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

	void ImportCapcodes(HWND dialog, CapcodeDialogState* state)
	{
		char path[MAX_PATH] = {};
		if (!ChooseCsvPath(dialog, false, path, sizeof(path))) return;
		std::ifstream input(path, std::ios::binary);
		if (!input) { MessageBoxA(dialog, "The CSV file could not be opened.", "PDW Capcode Directory", MB_ICONERROR); return; }
		std::string line;
		int imported = 0, rejected = 0, row = 0;
		for (;;)
		{
			const pdw::archive::CsvRecordReadResult readResult =
				pdw::archive::ReadCsvRecord(input, line);
			if (readResult == pdw::archive::CSV_RECORD_END) break;
			++row;
			if (readResult == pdw::archive::CSV_RECORD_MALFORMED)
			{
				++rejected;
				break;
			}
			if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
				static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
				line.erase(0, 3);
			std::vector<std::string> fields;
			if (!pdw::archive::ParseCsvLine(line, fields) || fields.size() < 2) { ++rejected; continue; }
			if (row == 1 && _stricmp(fields[0].c_str(), "protocol") == 0) continue;
			pdw::archive::CapcodeEntry entry;
			entry.protocol = fields[0] == "Any" ? "" : fields[0];
			entry.address = fields[1];
			if (fields.size() > 2) entry.displayName = fields[2];
			if (fields.size() > 3) entry.agency = fields[3];
			if (fields.size() > 4 && !fields[4].empty())
			{
				char* end = NULL;
				const unsigned long color = std::strtoul(fields[4].c_str(), &end, 10);
				if (!end || *end || color > 0x00ffffffUL) { ++rejected; continue; }
				entry.color = color;
			}
			if (fields.size() > 5) entry.notes = fields[5];
			if (fields.size() > 6) entry.enabled = fields[6] != "0" && _stricmp(fields[6].c_str(), "false") != 0;
			std::string error;
			if (MessageArchiveUpsertCapcode(entry, error)) ++imported; else ++rejected;
		}
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
		output << "protocol,address,display_name,agency,color,notes,enabled\r\n";
		for (std::size_t index = 0; index < state->entries.size(); ++index)
		{
			const pdw::archive::CapcodeEntry& entry = state->entries[index];
			output << pdw::archive::CsvEscape(entry.protocol.empty() ? "Any" : entry.protocol) << ','
				<< pdw::archive::CsvEscape(entry.address) << ','
				<< pdw::archive::CsvEscape(entry.displayName) << ','
				<< pdw::archive::CsvEscape(entry.agency) << ',' << entry.color << ','
				<< pdw::archive::CsvEscape(entry.notes) << ',' << (entry.enabled ? 1 : 0) << "\r\n";
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
		pdw::archive::HistoryQuery query;
		query.search = ControlText(dialog, IDC_HISTORY_SEARCH);
		query.protocol = SelectedProtocol(GetDlgItem(dialog, IDC_HISTORY_PROTOCOL));
		query.filteredOnly = IsDlgButtonChecked(dialog, IDC_HISTORY_FILTERED) != 0;
		query.limit = 200;
		query.offset = state->offset;
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
		strncpy(Profile.messageArchivePath, path.c_str(), sizeof(Profile.messageArchivePath) - 1);
		Profile.messageArchivePath[sizeof(Profile.messageArchivePath) - 1] = '\0';
		Profile.messageHistoryEnabled = IsDlgButtonChecked(dialog, IDC_HISTORY_ENABLE) != 0;
		Profile.messageHistoryIncludeMessage = IsDlgButtonChecked(dialog, IDC_HISTORY_MESSAGE) != 0;
		Profile.messageHistoryRetentionDays = retention;
		if (!Profile.messageHistoryEnabled) Profile.liveDashboardEnabled = 0;
		WriteSettings();
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
			AddListColumn(list, 2, "Display name", 180);
			AddListColumn(list, 3, "Agency / service", 180);
			AddListColumn(list, 4, "Enabled", 65);
			AddListColumn(list, 5, "Notes", 260);
			PopulateProtocolCombo(GetDlgItem(hDlg, IDC_CAPCODE_PROTOCOL), false);
			ClearCapcodeEditor(hDlg, state);
			RefreshCapcodes(hDlg, state);
			return TRUE;
		}

		case WM_NOTIFY:
			if (state && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_CAPCODE_LIST &&
				reinterpret_cast<NMHDR*>(lParam)->code == LVN_ITEMCHANGED)
				LoadSelectedCapcode(hDlg, state);
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
					pdw::archive::CapcodeEntry entry;
					entry.protocol = SelectedProtocol(GetDlgItem(hDlg, IDC_CAPCODE_PROTOCOL));
					entry.address = ControlText(hDlg, IDC_CAPCODE_ADDRESS);
					entry.displayName = ControlText(hDlg, IDC_CAPCODE_NAME);
					entry.agency = ControlText(hDlg, IDC_CAPCODE_AGENCY);
					entry.notes = ControlText(hDlg, IDC_CAPCODE_NOTES);
					entry.enabled = IsDlgButtonChecked(hDlg, IDC_CAPCODE_ENABLED) != 0;
					entry.color = state->selectedColor;
					std::string error;
					if (!MessageArchiveUpsertCapcode(entry, error)) SetUtf8Control(hDlg, IDC_CAPCODE_STATUS, error);
					else { RefreshCapcodes(hDlg, state); SetDlgItemTextA(hDlg, IDC_CAPCODE_STATUS, "Capcode saved; the raw address remains unchanged."); }
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
					if (!MessageArchiveDeleteCapcode(entry.protocol, entry.address, error)) SetUtf8Control(hDlg, IDC_CAPCODE_STATUS, error);
					else { ClearCapcodeEditor(hDlg, state); RefreshCapcodes(hDlg, state); }
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
				case IDC_HISTORY_SAVE_SETTINGS: SaveHistorySettings(hDlg); RefreshHistory(hDlg, state, true); return TRUE;
				case IDC_HISTORY_REFRESH: RefreshHistory(hDlg, state, true); return TRUE;
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
