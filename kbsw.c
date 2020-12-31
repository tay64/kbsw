// MINGW64:
// gcc -std=c11 -Wall -Werror -mwindows -O2 -flto -o kbsw.exe kbsw.c kbswhook.c mojibake.c docopt.c monospacebox.c
//     -DKBSW_STDOUT -- enable logging to stdout (run from mintty to see the output)

#include "version.h"
const char kUsage [] =
	"Command line: "PROG" [options] KEY=LAYOUT [KEY=LAYOUT...]\n"
	"\n"
	"where KEY can be one of the following:\n"
	"    LC  LCtrl   LeftCtrl   LeftControl\n"
	"    RC  RCtrl   RightCtrl  RightControl\n"
	"    LS  LShift  LeftShift\n"
	"    RS  RShift  RightShift\n"
	"    LA  LAlt    LeftAlt\n"
	"    RA  RAlt    RightAlt\n"
	"    LW  LWin    LeftWin\n"
	"    RW  RWin    RightWin\n"
	"    CL  Caps    CapsLock\n"
	"    NL          NumLock\n"
	"    SL          ScrollLock\n"
	"\n"
	"and LAYOUT codes can be obtained by running\n"
	"    "PROG" --list-layouts\n"
	"\n"
	"-t --timeout=300   KEY double-press timeout, in milliseconds\n"
	"-q --quiet         suppress error messages (only return error code)\n"
	"-x --exit          stop the running copy of "PROG"\n"
	"-p --pause         make the running instance stop doing anything\n"
	"-r --resume        make a paused running instance resume working\n"
	"-s --status        show parameters of the running instance, if any\n"
	"-l --list-layouts  display installed keyboard layouts and exit\n"
	"-h --help          show this text and exit\n"
	"\n"
	"Usage:\n"
	"\n"
	" - Press KEY twice quickly to switch to the corresponding keyboard LAYOUT.\n"
	"\n"
	" - To correct some text mistakenly typed in a wrong keyboard layout,\n"
	"   select it and press the correct layout's KEY quickly twice while\n"
	"   holding down any other modifier key (such as Shift, Alt, Ctrl).\n"
	"   This action overwrites the clipboard.\n"
	;

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <windows.h>
#include "docopt.h"
#include "common.h"
#include "kbswhook.h"
#include "mojibake.h"
#include "monospacebox.h"

typedef struct { char str[KL_NAMELENGTH]; } KLID;


const VKEY kModifierVKeys [] =
{
	VK_LSHIFT,
	VK_LCONTROL,
	VK_LMENU,
	VK_LWIN,
	VK_RSHIFT,
	VK_RCONTROL,
	VK_RMENU,
	VK_RWIN,
	0
};


// in the order of rising precedence: e.g. if Quit and Help are both specified, Help has effect
typedef enum
{
	cmdRun,
	cmdResume,
	cmdPause,
	cmdListLayouts,
	cmdShowStatus,
	cmdQuit,
	cmdHelp,
} Command;

enum { MAX_SWITCHES = 8 };

struct Options
{
	Command   command;
	unsigned  tap_timeout_ms;
	VKEY      keys     [MAX_SWITCHES];
	HKL       layouts  [MAX_SWITCHES];
	bool      quiet;
};


// returns 0 if not recognized
static VKEY ParseKeyName( const char* keyname )
{
	static const struct { const char* kw; VKEY vk; } key_names [] =
	{
		{ "LC", VK_LCONTROL },
		{ "RC", VK_RCONTROL },
		{ "LS", VK_LSHIFT },
		{ "RS", VK_RSHIFT },
		{ "LA", VK_LMENU },
		{ "RA", VK_RMENU },
		{ "LW", VK_LWIN },
		{ "RW", VK_RWIN },
		{ "CL", VK_CAPITAL },
		{ "NL", VK_NUMLOCK },
		{ "SL", VK_SCROLL },
	};

	const char* kw = DocOptFindLineWithWord(kUsage, "\n    ", keyname);
	if( kw == NULL )  return false;

	for( unsigned i = 0; i < COUNTOF(key_names); ++i )
	{
		if( strncmp(kw, key_names[i].kw, strlen(key_names[i].kw)) == 0 )
			return key_names[i].vk;
	}

	return 0;
}

// returns the new switch index, or -1 on error
static int AddLayoutSwitchKey( Options* po, VKEY vk )
{
	static_assert(COUNTOF(po->keys) == COUNTOF(po->layouts), "keys & layouts must be of same size");
	for( unsigned i = 0; i < COUNTOF(po->keys); ++i )
	{
		if( po->keys[i] == vk )
			return -1;  // no duplicate keys, please

		if( po->keys[i] == 0 )
		{
			po->keys[i] = vk;
			return i;
		}
	}
	return -1; // no room left
}

static bool ParseNonOptionArg( Options* po, const char* arg )
{
	char keyname [24];

	const char* eq = strchr(arg, '=');
	if( (eq == NULL) || (eq - arg >= sizeof(keyname)) )  return false;
	memcpy(keyname, arg, eq - arg);
	keyname[eq - arg] = 0;

	VKEY vk = ParseKeyName(keyname);
	if( vk == 0 )  return 0;

	// KLID is an 8-digit hex number, but it is not documented (except its length)
	// so we do not want to rely on that beyond treating leading zeros as not significant

	const char* val = eq;
	while( *++val == '0' ); // skip the leading zeros

	KLID klid;
	unsigned len = strlen(val);
	unsigned pad = COUNTOF(klid.str) - 1 - len;
	if( len >= COUNTOF(klid.str) )  return false;
	memset(klid.str, '0', pad);
	memcpy(klid.str + pad, val, len + 1);

	HKL hkl = LoadKeyboardLayoutA(klid.str, KLF_SUBSTITUTE_OK);
	if( hkl == NULL )  return ERR("LoadKeyboardLayout"), false;

	int idx = AddLayoutSwitchKey(po, vk);
	if( idx < 0 )  return false;

	po->layouts[idx] = hkl;
	return true;
}

// opt == 0 for non-option arguments
bool AppDocOptSetOption( Options* po, char opt, const char* val )
{
	Command cmd = po->command;
	switch( opt )
	{
		case 0:    return ParseNonOptionArg(po, val);

		case 'l':  cmd = cmdListLayouts; break;
		case 's':  cmd = cmdShowStatus; break;
		case 'p':  cmd = cmdPause; break;
		case 'r':  cmd = cmdResume; break;
		case 'x':  cmd = cmdQuit; break;
		case 'h':  cmd = cmdHelp; break;

		case 'q':  po->quiet = true; break;

		case 't':
			po->tap_timeout_ms = atoi(val);
			break;

		default: return false;
	}
	if( cmd > po->command )  po->command = cmd;
	return true;
}

void AppDocOptReportError( const char* arg )
{
	char buffer [1024];
	snprintf(buffer, sizeof(buffer), "Invalid command line argument:\n\n%s", arg);
	MessageBoxA(NULL, buffer, PROG, MB_OK | MB_ICONERROR);
}

// -----------------------------------------------------------------------------

static Options gOptions;


static int MessageLoop( void )
{
	MSG msg;
	while( GetMessageW(&msg, NULL, 0,0) )
	{
		DispatchMessageW(&msg);
	}
	return msg.wParam;
}

static HWND CreateMessageWindow( LPCWSTR class_name, WNDPROC wndproc )
{
	WNDCLASSW wc =
	{
		.hInstance = GetModuleHandle(NULL),
		.lpfnWndProc = wndproc,
		.lpszClassName = class_name,
	};
	ATOM wca = RegisterClassW(&wc);
	if( wca == 0 )  return ERR("RegisterClassW"), NULL;
	HWND wnd = CreateWindowW((LPCWSTR)(intptr_t)wca, L"msg", 0, 0,0, 0,0, HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL);
	if( wnd == NULL )  return ERR("CreateWindow"), NULL;
	return wnd;
}

static void MsgBox( const char* text, UINT flag )
{
	if( !gOptions.quiet )  MessageBoxA(NULL, text, PROG, MB_OK | flag);
}

// -----------------------------------------------------------------------------

// returns a pointer to an internal buffer that gets overwritten with each call
static const char* GetKeyboardLayoutText( const KLID* pklid )
{
	char path [256];
	snprintf(path, COUNTOF(path), "%s\\%s",
	         "SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts",
	         pklid->str);

	static char name [256];
	DWORD namesz = sizeof(name);
	LSTATUS err = RegGetValueA(HKEY_LOCAL_MACHINE, path, "Layout Text", RRF_RT_REG_SZ, NULL, name, &namesz);
	if( err != 0 )  { name[0] = '?'; name[1] = 0; }

	return name;
}

static void ShowKeyboardLayouts( void )
{
	HKL layouts [MAX_KEYBOARD_LAYOUTS];
	int n = GetKeyboardLayoutList(COUNTOF(layouts), layouts);
	if( n == 0 )
	{
		MsgBox("Failed to get keyboard layouts list", MB_ICONERROR);
		return;
	}

	char output [4096], *po = output;
	size_t remaining_size = COUNTOF(output);

	HKL initial_layout = GetKeyboardLayout(GetCurrentThreadId());
	for( int i = 0; i < n; ++i )
	{
		KLID klid;
		if( ActivateKeyboardLayout(layouts[i], 0) == NULL )  continue;
		if( !GetKeyboardLayoutNameA(klid.str) ) continue;

		int len = snprintf(po, remaining_size, "%*s   %s\n",
		                   (int)(COUNTOF(klid.str) - 1), klid.str,
		                   GetKeyboardLayoutText(&klid));
		if( len < 0 )
		{
			MsgBox("Formatting failed (?)", MB_ICONERROR);
			ActivateKeyboardLayout(initial_layout, 0);
			return;
		}

		po += len;
		remaining_size -= len;
	}
	ActivateKeyboardLayout(initial_layout, 0);

	MonospaceBox(PROG, output);
}

// -----------------------------------------------------------------------------

static HWND SetFocusedWindowLayout( HKL new_layout, bool translate_mojibake )
{
	HWND target = GetForegroundWindow();
	if( target == NULL )  return ERR("GetForegroundWindow"), NULL;

	DWORD fg_thread = GetWindowThreadProcessId(target, NULL);
	if( fg_thread == 0 )  return ERR("GetWindowThreadProcessId"), NULL;

	GUITHREADINFO gti;
	gti.cbSize = sizeof(gti);
	if( GetGUIThreadInfo(fg_thread, &gti) && gti.hwndFocus )  target = gti.hwndFocus;

	if( translate_mojibake )
	{
		MojibakeTranslateSelection(target, new_layout);
	}

	PostMessage(target, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)new_layout);
	return target;
}

// -----------------------------------------------------------------------------

enum
{
	UWM_ACTIVATE_LAYOUT = WM_USER,
	UWM_PAUSE_RESUME,               // wParam: false to pause, true to resume
};

static const WCHAR kMainWindowClassName [] = L""PROG".main.6qZK6nb0dYxsgS6H4b8w";
static const WCHAR kHookWindowClassName [] = L""PROG".hook.6qZK6nb0dYxsgS6H4b8w";

static HWND ghMainWindow = NULL;


HWND AppHookCreateMessageWindow( WNDPROC wndproc )
{
	return CreateMessageWindow(kHookWindowClassName, wndproc);
}

void AppHookMessageLoop( void )
{
	MessageLoop();
}

void AppHookNotify( unsigned idx, bool any_modifier_pressed )
{
	if( idx >= COUNTOF(gOptions.layouts) )  return;
	HKL new_layout = gOptions.layouts[idx];
//	LOG("%p", new_layout);
	PostMessage(ghMainWindow, UWM_ACTIVATE_LAYOUT, any_modifier_pressed, (LPARAM)new_layout);
}


static LRESULT CALLBACK MainWindowProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	switch( msg )
	{
		case WM_CREATE:
			if( !HookStart() )  return -1;
			if( !AddClipboardFormatListener(hwnd) )  ERR("AddClipboardFormatListener");
			break;

		case WM_DESTROY:
			HookShutdown();
			RemoveClipboardFormatListener(hwnd);
			LOG("%p exited", hwnd);
			break;

		case WM_CLIPBOARDUPDATE:
			LOG("WM_CLIPBOARDUPDATE");
			MojibakeOnClipboardUpdate(hwnd);
			break;

		case WM_GETTEXT:
			return snwprintf((WCHAR*)lParam, wParam, L"%ls", GetCommandLineW());

		case UWM_ACTIVATE_LAYOUT:
			//TODO: SHQueryUserNotificationState
			if( MojibakeIsBusy() )  return LOG("busy"), 0;
			SetFocusedWindowLayout((HKL)lParam, wParam);
			return 0;

		case UWM_PAUSE_RESUME:
			return HookPauseResume(!!wParam);
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------

static HWND FindRunningInstance( void )
{
	return FindWindowW(kMainWindowClassName, NULL);
}

static bool StopRunningInstance( HWND hwnd_running )
{
	HWND running = hwnd_running ? hwnd_running : FindRunningInstance();
	if( running )
	{
		LOG("stopping %p", running);
		PostMessageW(running, WM_QUIT, 0, 0);
	}
	else
	{
		LOG("not running");
	}
	return !!running;
}

static bool Run( const Options* opt )
{
	HWND running = FindRunningInstance();

	HookConfigure(opt->keys, COUNTOF(opt->keys), opt->tap_timeout_ms);

	ghMainWindow = CreateMessageWindow(kMainWindowClassName, MainWindowProc);
	if( ghMainWindow == NULL )
		return false;

	if( running )  StopRunningInstance(running);

	int rc = MessageLoop();

	HookShutdown();
	return rc == 0;
}


static bool ShowRunningInstanceStatus( void )
{
	HWND running = FindRunningInstance();
	if( running )
	{
		char buffer [256] = PROG" is running.\n\nCommand line:\n\n";
		SendMessageA(running, WM_GETTEXT, COUNTOF(buffer) - strlen(buffer), (LPARAM)(buffer + strlen(buffer)));
		MsgBox(buffer, MB_ICONINFORMATION);
	}
	else
	{
		MsgBox(PROG" is not running.", MB_ICONINFORMATION);
	}
	return running;
}


static bool PauseResume( Command cmd )
{
	HWND running = FindRunningInstance();
	if( running )
	{
		if( !SendMessageA(running, UWM_PAUSE_RESUME, cmd == cmdResume, 0) )
			return MsgBox("Failed to pause/resume", MB_ICONERROR), false;
	}
	else
	{
		MsgBox(PROG" is not running.", MB_ICONINFORMATION);
	}
	return running;
}


int main( int argc, char* argv[] )
{
	gOptions.command = cmdRun;
	if( !DocOptParseCommandLine(&gOptions, kUsage, argc, argv) && (gOptions.command != cmdHelp) )
		return 1;

	switch( gOptions.command )
	{
		case cmdRun:
			if( gOptions.keys[0] == 0 )
			{
				MessageBoxA(NULL, "No switches specified on command line.\n"
				                  "Nothing to do.\n\n"
				                  "Run '"PROG" --help' for a brief usage description.",
				            PROG, MB_OK | MB_ICONERROR);
				return 1;
			}
			if( !Run(&gOptions) )
			{
				MsgBox("Something went wrong.\n"PROG" failed to start.", MB_ICONERROR);
				return 1;
			}
			return 0;

		case cmdPause:
		case cmdResume:
			return PauseResume(gOptions.command) ? 0 : 1;

		case cmdQuit:
			return StopRunningInstance(NULL) ? 0 : 1;

		case cmdListLayouts:
			ShowKeyboardLayouts();
			return 0;

		case cmdShowStatus:
			return ShowRunningInstanceStatus() ? 0 : 1;

		case cmdHelp:
			MonospaceBox(PROG" "PROG_VERSION, kUsage);
			return 0;
	}

	return -1;
}
