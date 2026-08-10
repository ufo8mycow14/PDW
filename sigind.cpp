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
	si_index=0;  // this is used by UpdateSigInd().
	if (got_sigind) show_sigind(0, 0);
}

// Update signal indicator on toolbar.
// Draws a new line on signal indicator,
// removing old line first.
void UpdateSigInd(int direction_flg)
{
	si_old_index = si_index;

	if (old_rect_flg)
	{
		if (direction_flg)	// Move indictor right 1
		{
			si_low_hover=0;
			si_index ? si_index+=2 : si_index++;

			if (si_index > MAX_SI_POS)
			{
				si_hi_hover++;
				si_index=MAX_SI_POS;
				return;
			}
	 	}
		else
		{							// Move indictor left 1
			if (si_low_hover)
			{
				if (si_low_hover==1)
				{
					show_sigind(1, 0);
				}
				if (si_low_hover > 1)
				{
					si_low_hover=0;
					show_sigind(0, 1);
				}
			}
			si_hi_hover=0;
			si_index--;

			if (si_index < 0)
			{
				si_low_hover++;
				si_index=0;
				return;
			}
		}
		show_sigind(si_index, si_old_index);
	}
}


// Draw signal indicator needle.
// Draw needle at new_pos.
// old_pos is used to erase previous line.
void DrawToolbarIndicators(HDC hdc)
{
	RECT client;
	GetClientRect(hToolbar, &client);
	extern double dRX_Quality;

	RECT status = { client.right - 75, 4, client.right - 50, 28 };
	HBRUSH statusBackground = CreateSolidBrush(PdwThemeSurfaceColor());
	HPEN statusBorder = CreatePen(PS_SOLID, 1, PdwThemeBorderColor());
	HGDIOBJ oldBrush = SelectObject(hdc, statusBackground);
	HGDIOBJ oldPen = SelectObject(hdc, statusBorder);
	RoundRect(hdc, status.left, status.top, status.right, status.bottom, 5, 5);

	if (dRX_Quality && dRX_Quality < 90)
	{
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(255, 185, 0));
		if (PdwThemeUiSemiboldFont()) SelectObject(hdc, PdwThemeUiSemiboldFont());
		TextOut(hdc, client.right - 66, 7, "!", 1);
	}
	else if (dRX_Quality == 0)
	{
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, PdwThemeMutedTextColor());
		if (PdwThemeUiSemiboldFont()) SelectObject(hdc, PdwThemeUiSemiboldFont());
		TextOut(hdc, client.right - 68, 7, "Q", 1);
	}
	else
	{
		HPEN goodPen = CreatePen(PS_SOLID, 2, RGB(16, 124, 16));
		SelectObject(hdc, goodPen);
		MoveToEx(hdc, client.right - 68, 16, NULL);
		LineTo(hdc, client.right - 64, 20);
		LineTo(hdc, client.right - 57, 11);
		SelectObject(hdc, statusBorder);
		DeleteObject(goodPen);
	}

	RECT gauge = { client.right - 47, 4, client.right - 5, 28 };
	HBRUSH background = CreateSolidBrush(PdwThemeSurfaceColor());
	HPEN border = CreatePen(PS_SOLID, 1, PdwThemeBorderColor());
	SelectObject(hdc, background);
	SelectObject(hdc, border);
	RoundRect(hdc, gauge.left, gauge.top, gauge.right, gauge.bottom, 5, 5);

	int activeBars = min(5, max(0, (si_index + 3) / 4));
	for (int i = 0; i < 5; ++i)
	{
		int height = 4 + i * 3;
		RECT bar = { gauge.left + 6 + i * 6, gauge.bottom - 5 - height,
			gauge.left + 10 + i * 6, gauge.bottom - 5 };
		HBRUSH barBrush = CreateSolidBrush(i < activeBars ?
			PdwThemeAccentColor() : PdwThemeBorderColor());
		FillRect(hdc, &bar, barBrush);
		DeleteObject(barBrush);
	}

	SelectObject(hdc, oldBrush);
	SelectObject(hdc, oldPen);
	DeleteObject(statusBackground);
	DeleteObject(statusBorder);
	DeleteObject(background);
	DeleteObject(border);
}

void show_sigind(int new_pos,int old_pos)
{
	(void)new_pos;
	(void)old_pos;
	if (!hToolbar) return;
	HDC hdc = GetDC(hToolbar);
	DrawToolbarIndicators(hdc);
	ReleaseDC(hToolbar, hdc);
}

