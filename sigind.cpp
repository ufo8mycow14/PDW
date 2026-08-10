//
// Sigind.cpp
//
// This file contains functions for displaying/updating
// the signal indicator.
//
#ifndef STRICT
#define STRICT 1
#endif

#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <stdio.h>
#include <commdlg.h>
#include <string.h>
#include <time.h>


#include "headers\resource.h"
#include "headers\PDW.h"
#include "headers\slicer.h"
#include "headers\toolbar.h"
#include "headers\gfx.h"
#include "headers\initapp.h"
#include "headers\live_signal_meter.h"
#include "headers\sigind.h"
#include "headers\ui_theme.h"

#define MAX_SI_POS        20	// 0-12. Max positions available to signal indicator.
#define AUDIO_POINT_VALUE 2	// Used for working out samples per signal
										// position.e.g. sample=20/10, position=2.

BOOL old_rect_flg=FALSE;
BOOL got_sigind=FALSE;
HDC hdcMemory=NULL;

HBITMAP hbm_sigind=NULL;
BITMAP bms;
RECT old_rect;     // used for redrawing signal indicator bitmap
RECT sig_rect;     // used for holding current position of sigind bitmap

// Keep track of signal indicator.
int si_index=0;
int si_old_index=0;
int delay_cnt=0;
int si_low_hover=0;
int si_hi_hover=0;

// Points used for drawing signal indicator.

int sip[21][4] =	{//from: x,  y, To: x,  y
							{18, 20,  6, 14},
//							{18, 20,  7, 13},
							{18, 20,  8, 12},
							{18, 20,  9, 11},
							{18, 20, 10, 10},
							{18, 20, 11,  9},
							{18, 20, 12,  9},
							{18, 20, 13,  8},
							{18, 20, 15,  8},
							{18, 20, 16,  7},
							{18, 20, 17,  7},
							{18, 20, 19,  7},
							{18, 20, 20,  7},
							{18, 20, 22,  8},
							{18, 20, 23,  8},
							{18, 20, 24,  9},
							{18, 20, 25,  9},
							{18, 20, 26, 10},
							{18, 20, 27, 11},
							{18, 20, 28, 12},
							{18, 20, 29, 13},
							{18, 20, 30, 14},
					};


// Get signal indicator bitmap resource
BOOL LoadSigInd(HINSTANCE hThisInstance)
{
	(void)hThisInstance;
	// The original indicator was a fixed bitmap with a white background.
	// It is now rendered with theme-aware GDI shapes.
	got_sigind = TRUE;
	return(got_sigind);
}

// Free bitmap object
void FreeSigInd(void)
{
	got_sigind = FALSE;
}

// Draw signal indicator on toolbar
void DrawSigInd(HWND hwnd)
{
	(void)hwnd;
}

// Update signal indicator on toolbar.
// Draws a new line on signal indicator,
// removing old line first.
void UpdateSigInd(int direction_flg)
{
	// Legacy serial/slicer paths do not expose normalized audio samples. Record
	// their real transitions for the new UI meter instead of painting from the
	// decoder thread.
	LiveSignalMeterNoteLegacyActivity(direction_flg);
}


// Draw signal indicator needle.
// Draw needle at new_pos.
// old_pos is used to erase previous line.
void DrawToolbarIndicators(HDC hdc)
{
	(void)hdc;
}

void show_sigind(int new_pos,int old_pos)
{
	(void)new_pos;
	(void)old_pos;
}

