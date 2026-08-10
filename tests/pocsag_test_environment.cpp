#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>

#include "headers/pdw.h"
#include "headers/misc.h"
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

	unsigned int EccContribution(unsigned int index)
	{
		unsigned int shift = 0x3B4;
		for (unsigned int current = 0; current < index; ++current)
			shift = (shift & 1u) ? ((shift >> 1) ^ 0x3B4u) : (shift >> 1);
		return shift;
	}

	int TenBitOnes(unsigned int value)
	{
		int count = 0;
		for (int bit = 0; bit < 10; ++bit)
		{
			if (value & 1u) ++count;
			value >>= 1;
		}
		return count;
	}
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

int nOnes(int value)
{
	unsigned int bits = static_cast<unsigned int>(value);
	int count = 0;
	for (int bit = 0; bit < 16; ++bit)
	{
		if (bits & 1u) ++count;
		bits >>= 1;
	}
	return count;
}

int ecd()
{
	unsigned int calculatedEcc = 0;
	unsigned int receivedEcc = 0;
	int parity = 0;

	for (unsigned int bit = 0; bit <= 20; ++bit)
	{
		if (ob[bit] == 1)
		{
			calculatedEcc ^= EccContribution(bit);
			parity ^= 1;
		}
	}
	for (unsigned int bit = 21; bit <= 30; ++bit)
	{
		receivedEcc <<= 1;
		if (ob[bit] == 1) receivedEcc ^= 1u;
	}

	int errors = 0;
	if ((calculatedEcc ^ receivedEcc) != 0)
	{
		// These fixtures deliberately cover only clean codewords. Marking a
		// syndrome as uncorrectable prevents this test shim from masquerading as
		// production BCH correction evidence.
		g_invalidCodeword = true;
		errors = 3;
	}
	else
	{
		parity = (parity + TenBitOnes(calculatedEcc)) & 1;
		if (parity != ob[31])
		{
			g_invalidCodeword = true;
			errors = 1;
		}
	}

	CountBiterrors(errors);
	return errors;
}

void CountBiterrors(int errors)
{
	g_errorObservations.push_back(errors);
}

void InvertData(void)
{
	Profile.invert ^= 1u;
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
