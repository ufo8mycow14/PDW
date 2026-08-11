#include "headers\multi_channel.h"

#include <commctrl.h>

#include <cstdio>
#include <vector>

#include "headers\pdw.h"
#include "headers\resource.h"
#include "multi_channel_manager.h"

namespace
{
	struct DialogState
	{
		std::vector<pdw::multichannel::ChannelConfig> channels;
		int selectedIndex;
		DialogState() : channels(pdw::multichannel::LoadChannels()), selectedIndex(0) {}
	};

	std::string Text(HWND dialog, int control)
	{
		char value[256] = {};
		GetDlgItemTextA(dialog, control, value, sizeof(value));
		return value;
	}

	void AddColumn(HWND list, int column, const char* text, int width)
	{
		LVCOLUMNA item = {};
		item.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		item.iSubItem = column;
		item.cx = width;
		item.pszText = const_cast<char*>(text);
		ListView_InsertColumn(list, column, &item);
	}

	void SetListText(HWND list, int row, int column, const char* text)
	{
		if (column == 0)
		{
			LVITEMA item = {};
			item.mask = LVIF_TEXT;
			item.iItem = row;
			item.pszText = const_cast<char*>(text);
			ListView_InsertItem(list, &item);
		}
		else ListView_SetItemText(list, row, column, const_cast<char*>(text));
	}

	void RefreshList(HWND dialog, DialogState* state)
	{
		HWND list = GetDlgItem(dialog, IDC_MULTI_LIST);
		ListView_DeleteAllItems(list);
		for (std::size_t index = 0; index < state->channels.size(); ++index)
		{
			const pdw::multichannel::ChannelConfig& channel = state->channels[index];
			char slot[12] = {}, endpoint[128] = {}, frequency[32] = {};
			std::snprintf(slot, sizeof(slot), "%u", static_cast<unsigned int>(index + 1));
			if (channel.source == AUDIO_SOURCE_RTL_TCP) std::snprintf(endpoint, sizeof(endpoint), "%s:%u", channel.host.c_str(), channel.port);
			else std::snprintf(endpoint, sizeof(endpoint), "RTL-SDR device %u", channel.deviceIndex);
			std::snprintf(frequency, sizeof(frequency), "%u", channel.frequencyHz);
			SetListText(list, static_cast<int>(index), 0, slot);
			SetListText(list, static_cast<int>(index), 1, channel.enabled ? "Yes" : "No");
			SetListText(list, static_cast<int>(index), 2, channel.label.c_str());
			SetListText(list, static_cast<int>(index), 3, channel.source == AUDIO_SOURCE_RTL_TCP ? "rtl_tcp" : "RTL-SDR");
			SetListText(list, static_cast<int>(index), 4, endpoint);
			SetListText(list, static_cast<int>(index), 5, frequency);
			const std::string status = pdw::multichannel::ChannelStatus(static_cast<unsigned int>(index));
			SetListText(list, static_cast<int>(index), 6, status.c_str());
		}
		ListView_SetItemState(list, state->selectedIndex, LVIS_SELECTED | LVIS_FOCUSED,
			LVIS_SELECTED | LVIS_FOCUSED);
	}

	void LoadEditor(HWND dialog, DialogState* state)
	{
		const pdw::multichannel::ChannelConfig& channel = state->channels[state->selectedIndex];
		CheckDlgButton(dialog, IDC_MULTI_ENABLE, channel.enabled ? BST_CHECKED : BST_UNCHECKED);
		SetDlgItemTextA(dialog, IDC_MULTI_LABEL, channel.label.c_str());
		SendDlgItemMessage(dialog, IDC_MULTI_SOURCE, CB_SETCURSEL, channel.source == AUDIO_SOURCE_RTL_SDR ? 1 : 0, 0);
		SetDlgItemTextA(dialog, IDC_MULTI_HOST, channel.host.c_str());
		SetDlgItemInt(dialog, IDC_MULTI_PORT, channel.port, FALSE);
		SetDlgItemInt(dialog, IDC_MULTI_DEVICE, channel.deviceIndex, FALSE);
		SetDlgItemInt(dialog, IDC_MULTI_FREQUENCY, channel.frequencyHz, FALSE);
	}

	void SaveEditor(HWND dialog, DialogState* state)
	{
		pdw::multichannel::ChannelConfig& channel = state->channels[state->selectedIndex];
		channel.enabled = IsDlgButtonChecked(dialog, IDC_MULTI_ENABLE) != 0;
		channel.label = Text(dialog, IDC_MULTI_LABEL);
		channel.source = SendDlgItemMessage(dialog, IDC_MULTI_SOURCE, CB_GETCURSEL, 0, 0) == 1 ?
			AUDIO_SOURCE_RTL_SDR : AUDIO_SOURCE_RTL_TCP;
		channel.host = Text(dialog, IDC_MULTI_HOST);
		BOOL translated = FALSE;
		channel.port = GetDlgItemInt(dialog, IDC_MULTI_PORT, &translated, FALSE);
		channel.deviceIndex = GetDlgItemInt(dialog, IDC_MULTI_DEVICE, &translated, FALSE);
		channel.frequencyHz = GetDlgItemInt(dialog, IDC_MULTI_FREQUENCY, &translated, FALSE);
	}
}

BOOL FAR PASCAL MultiChannelDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	DialogState* state = reinterpret_cast<DialogState*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			state = new DialogState();
			SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
			HWND list = GetDlgItem(hDlg, IDC_MULTI_LIST);
			ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
			AddColumn(list, 0, "Slot", 38); AddColumn(list, 1, "On", 34);
			AddColumn(list, 2, "Label", 96); AddColumn(list, 3, "Source", 66);
			AddColumn(list, 4, "Receiver", 130); AddColumn(list, 5, "Frequency", 86);
			AddColumn(list, 6, "Status", 64);
			SendDlgItemMessageA(hDlg, IDC_MULTI_SOURCE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("rtl_tcp receiver"));
			SendDlgItemMessageA(hDlg, IDC_MULTI_SOURCE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Direct RTL-SDR"));
			RefreshList(hDlg, state);
			LoadEditor(hDlg, state);
			return TRUE;
		}
		case WM_NOTIFY:
			if (state && reinterpret_cast<LPNMHDR>(lParam)->idFrom == IDC_MULTI_LIST &&
				reinterpret_cast<LPNMHDR>(lParam)->code == LVN_ITEMCHANGED)
			{
				const int selectedIndex = ListView_GetNextItem(GetDlgItem(hDlg, IDC_MULTI_LIST), -1, LVNI_SELECTED);
				if (selectedIndex >= 0 && selectedIndex < 4 && selectedIndex != state->selectedIndex)
				{
					SaveEditor(hDlg, state);
					state->selectedIndex = selectedIndex;
					LoadEditor(hDlg, state);
				}
			}
			return TRUE;
		case WM_COMMAND:
			if (!state) break;
			switch (LOWORD(wParam))
			{
				case IDC_MULTI_SAVE:
				{
					SaveEditor(hDlg, state);
					std::string error;
					if (!pdw::multichannel::SaveChannels(state->channels, error))
						SetDlgItemTextA(hDlg, IDC_MULTI_STATUS, error.c_str());
					else SetDlgItemTextA(hDlg, IDC_MULTI_STATUS, "Channel configuration saved. No workers were started.");
					RefreshList(hDlg, state);
					return TRUE;
				}
				case IDC_MULTI_START:
				{
					SaveEditor(hDlg, state);
					std::string error;
					if (!pdw::multichannel::LaunchEnabledChannels(state->channels, error))
						SetDlgItemTextA(hDlg, IDC_MULTI_STATUS, error.c_str());
					else SetDlgItemTextA(hDlg, IDC_MULTI_STATUS, "Enabled channels launched as isolated PDW workers.");
					RefreshList(hDlg, state);
					return TRUE;
				}
				case IDC_MULTI_STOP:
				{
					std::string status;
					pdw::multichannel::StopAllChannels(status);
					SetDlgItemTextA(hDlg, IDC_MULTI_STATUS, status.c_str());
					RefreshList(hDlg, state);
					return TRUE;
				}
				case IDOK:
				case IDCANCEL: EndDialog(hDlg, LOWORD(wParam)); return TRUE;
			}
			break;
		case WM_DESTROY:
			delete state;
			SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
			return TRUE;
	}
	return FALSE;
}
