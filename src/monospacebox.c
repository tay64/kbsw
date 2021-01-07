#include <stdbool.h>
#include <windows.h>
#include "monospacebox.h"

static struct
{
	DLGTEMPLATE header;
	WORD no_menu;
	WORD std_class;
	WCHAR empty_title;
	DLGITEMTEMPLATE text_di;
	WORD text_class [2];
	WORD text_notext;
	WORD text_cdata;
} kMonospaceBoxDialogTemplate =
{
	{
		.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | DS_CENTER,
		.cdit = 1,
		.cx = 400,
		.cy = 500,
	},
	0,
	0,
	.text_di =
	{
		.style = WS_CHILD | WS_VISIBLE | SS_EDITCONTROL,
		.x = 4,
		.y = 4,
		.cx = 392,
		.cy = 492,
		.id = 1,
	},
	.text_class = { 0xffff, 0x0082 }, // STATIC
};

struct MonospaceBoxInit
{
	const char* caption;
	const char* text;
};

static void MonospaceBoxInit( HWND hdlg, HWND textbox, const char* text )
{
	const int font_size_pt = 12;
	HDC dc = GetDC(textbox);
	if( dc == NULL )  return;

	LOGFONTA lfont =
	{
		.lfHeight = -MulDiv(font_size_pt, GetDeviceCaps(dc, LOGPIXELSY), 72),
		.lfCharSet = DEFAULT_CHARSET,
		.lfPitchAndFamily = FIXED_PITCH,
		.lfFaceName = "Consolas",
	};
	HFONT hf = CreateFontIndirectA(&lfont), saved_font = NULL;
	if( hf )
	{
		SendMessage(textbox, WM_SETFONT, (WPARAM)hf, TRUE);
		saved_font = SelectObject(dc, hf);
	}

	RECT rc_textbox, rc_dlg;
	GetWindowRect(hdlg, &rc_dlg);
	GetClientRect(textbox, &rc_textbox);
	LONG xmargin = rc_dlg.right - rc_dlg.left - rc_textbox.right;
	LONG ymargin = rc_dlg.bottom - rc_dlg.top - rc_textbox.bottom;

	// measure the text and resize the dialog, preserving the margins
	DrawTextA(dc, text, -1, &rc_textbox, DT_CALCRECT | DT_EDITCONTROL | DT_EXPANDTABS);
	SetWindowPos(hdlg, NULL, 0,0, rc_textbox.right + xmargin,
	                              rc_textbox.bottom + ymargin,
	             SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);

	if( saved_font )  SelectObject(dc, saved_font);
	ReleaseDC(textbox, dc);
}

static INT_PTR MonospaceBoxProc( HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam )
{
	switch( msg )
	{
		case WM_INITDIALOG:
		{
			const struct MonospaceBoxInit* p = (const struct MonospaceBoxInit*)lParam;
			HWND textbox = (HWND) wParam;
			SetWindowTextA(hdlg, p->caption);
			SetWindowTextA(textbox, p->text);
			MonospaceBoxInit(hdlg, textbox, p->text);
			break;
		}
		case WM_DESTROY:
		{
			HFONT hf = (HFONT)SendMessage(GetDlgItem(hdlg, 1), WM_GETFONT, 0, 0);
			if( hf )  DeleteObject(hf);
			break;
		}
		case WM_COMMAND: case WM_SYSCOMMAND:
		{
			bool close = (msg == WM_SYSCOMMAND)
			           ? (wParam == SC_CLOSE)
			           : ((wParam == IDOK) || (wParam == IDCANCEL));
			if( close )  EndDialog(hdlg, 0);
			break;
		}
	}
	return FALSE;
}

int MonospaceBox( const char* caption, const char* text )
{
	struct MonospaceBoxInit p = { caption, text };
	return DialogBoxIndirectParamW(NULL, &kMonospaceBoxDialogTemplate.header, NULL,
	                               MonospaceBoxProc, (LPARAM)&p);
}
