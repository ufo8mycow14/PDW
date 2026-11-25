#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include "headers/pdw.h"
#include "headers/misc.h"
#include "headers/sound_in.h"
#include "headers/helper_funcs.h"
#include "pocsag_test_environment.h"

#include <cstring>
#include <string>
#include <vector>

PROFILE Profile;
PaneStruct Pane1;
PaneStruct Pane2;

int pocsag_baud_rate = STAT_POCSAG1200;
int pocbit = 0;
char ob[32] = { 0 };
unsigned int bch[1025] = {}, ecs[25] = {};
char high_audio = DEFAULT_HI_AUDIO, low_audio = DEFAULT_LO_AUDIO, atb_bit = 0;
int sync_bit = DEFAULT_LO_AUDIO;

unsigned long hourly_stat[NUM_STAT][2] = { 0 };
unsigned long hourly_char[NUM_STAT][2] = { 0 };
unsigned long daily_stat[NUM_STAT][2] = { 0 };
unsigned long daily_char[NUM_STAT][2] = { 0 };

char Current_MSG[9][MAX_STR_LEN] = { { 0 } };
unsigned char message_buffer[MAX_STR_LEN + 1] = { 0 };
unsigned char mobitex_buffer[MAX_STR_LEN + 1] = { 0 };
BYTE message_color[MAX_STR_LEN + 1] = { 0 };
BYTE messageitems_colors[7] = { 0 };
int iMessageIndex = 0;
char* dsc_pchar = NULL;
BYTE* dsc_pcolor = NULL;

char aNumeric[17] = "0123456789*U -][";
char szCurrentDate[40] = "01-01-00";
char szCurrentTime[40] = "00:00:00";

namespace
{
	std::string g_payload;
	std::vector<pdw_test::CapturedPocsagMessage> g_messages;
	std::vector<int> g_errorObservations;
	bool g_invalidCodeword = false;
	bool g_changedPolarity = false;

}

namespace pdw_test
{
	void ResetPocsagEnvironment()
	{
		// PROFILE owns std::vector members, so reset it through normal C++
		// assignment rather than treating it as a plain C structure.
		Profile = PROFILE();
		std::memset(&Pane1, 0, sizeof(Pane1));
		std::memset(&Pane2, 0, sizeof(Pane2));
		std::memset(Current_MSG, 0, sizeof(Current_MSG));
		std::memset(messageitems_colors, 0, sizeof(messageitems_colors));
		std::memset(hourly_stat, 0, sizeof(hourly_stat));
		std::memset(hourly_char, 0, sizeof(hourly_char));
		std::memset(daily_stat, 0, sizeof(daily_stat));
		std::memset(daily_char, 0, sizeof(daily_char));
		std::memset(ob, 0, sizeof(ob));

		Profile.showtone = 1;
		Profile.shownumeric = 1;
		Profile.pocsag_fnu = 1;
		Profile.pocsag_showboth = 0;
		Profile.invert = 0;
		high_audio = DEFAULT_HI_AUDIO;
		low_audio = DEFAULT_LO_AUDIO;
		setupecc();
		pocsag_baud_rate = STAT_POCSAG1200;
		pocbit = 0;
		g_payload.clear();
		g_messages.clear();
		g_errorObservations.clear();
		g_invalidCodeword = false;
		g_changedPolarity = false;
	}

	const std::vector<CapturedPocsagMessage>& CapturedPocsagMessages()
	{
		return g_messages;
	}

	const std::vector<int>& PocsagBitErrorObservations()
	{
		return g_errorObservations;
	}

	bool PocsagFixtureHadInvalidCodeword()
	{
		return g_invalidCodeword;
	}

	bool PocsagFixtureChangedPolarity()
	{
		return g_changedPolarity;
	}
}

static void CountPagingBchWord(int errors)
{
	if (errors >= 3) g_invalidCodeword = true;
	CountBiterrors(errors);
}

#define InvertData InvertFixturePolarity
#include "bch.inc"
#undef InvertData

void CountBiterrors(int errors)
{
	g_errorObservations.push_back(errors);
}

void InvertData(void)
{
	InvertFixturePolarity();
	g_changedPolarity = true;
}

void Get_Date_Time(void)
{
	std::strcpy(szCurrentDate, "01-01-00");
	std::strcpy(szCurrentTime, "00:00:00");
}

void display_color(PaneStruct* pane, BYTE color)
{
	if (pane) pane->currentColor = color;
}

void display_show_char(PaneStruct*, char value)
{
	g_payload.push_back(value);
}

void display_show_str(PaneStruct*, char value[])
{
	if (value) g_payload += value;
}

void ShowMessage()
{
	pdw_test::CapturedPocsagMessage captured;
	captured.address = Current_MSG[MSG_CAPCODE];
	captured.mode = Current_MSG[MSG_MODE];
	captured.messageType = Current_MSG[MSG_TYPE];
	captured.bitrate = Current_MSG[MSG_BITRATE];
	captured.payload = g_payload;
	g_messages.push_back(captured);
	g_payload.clear();
}
