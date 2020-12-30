// MINGW64:
// gcc -std=c11 -Wall -Werror -mwindows -O2 -flto -o kbsw.exe kbsw.c kbswhook.c docopt.c monospacebox.c
//     -DKBSW_STDOUT -- enable logging to stdout (run from mintty to see the output)

#include "version.h"
const char kUsage [] =
	"Usage: "PROG" [options] KEY=LAYOUT [KEY=LAYOUT...]\n"
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
	"\n"
	"and LAYOUT codes can be obtained by running\n"
	"    "PROG" --list-layouts\n"
	"\n"
	"-t --timeout=300   double-tap timeout, in milliseconds\n"
	"-x --exit          stop the running copy of "PROG"\n"
	"-p --pause         make the running instance stop doing anything\n"
	"-r --resume        make a paused running instance resume working\n"
	"-R --restart       stop the running instance and start a new one\n"
	"                   with same parameters\n"
	"-s --status        show parameters of the running instance, if any\n"
	"-l --list-layouts  display installed keyboard layouts and exit\n"
	"-h --help          show this text and exit\n"
	;

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <windows.h>
#include "docopt.h"
#include "common.h"
#include "kbswhook.h"
#include "monospacebox.h"

#define MAX_LAYOUTS  8

// in the order of rising precedence: e.g. if Quit and Help are both specified, Help has effect
typedef enum
{
	cmdRun,
	cmdListLayouts,
	cmdShowStatus,
	cmdQuit,
	cmdHelp,
} Command;

struct Options
{
	HookParameters keyboard;
	HKL layouts [HOOK_MAX_SWITCHES];
	Command command;
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

static bool AddLayoutSwitch( Options* po, VKEY vk, HookSwitchId layout )
{
	for( unsigned i = 0; i < COUNTOF(po->keyboard.switches); ++i )
	{
		if( po->keyboard.switches[i].vk == vk )
			return false;  // no duplicate keys, please

		if( po->keyboard.switches[i].vk == 0 )
		{
			po->keyboard.switches[i].vk = vk;
			po->keyboard.switches[i].id = layout;
			return true;
		}
	}
	return false; // no room left
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

	char* eptr = NULL;
	unsigned long layout = strtoul(eq + 1, &eptr, 16);
	if( !eptr || *eptr != 0 )  return false;

	return AddLayoutSwitch(po, vk, (HookSwitchId)layout);
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
		case 'x':  cmd = cmdQuit; break;
		case 'h':  cmd = cmdHelp; break;

		case 't':
			po->keyboard.tap_timeout_ms = atoi(val);
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

int MessageLoop( void )
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

// -----------------------------------------------------------------------------

// returns a pointer to an internal buffer that gets overwritten with each call
static const char* GetKeyboardLayoutText( HKL hkl )
{
	HKL active_layout = ActivateKeyboardLayout(hkl, 0);
	if( active_layout == NULL )  return "(unknown)";

	char tag [KL_NAMELENGTH];
	if( !GetKeyboardLayoutNameA(tag) )  tag[0] = 0;

	char path [256];
	snprintf(path, COUNTOF(path), "%s\\%s", "SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", tag);

	static char name [256];
	DWORD namesz = sizeof(name);
	LSTATUS err = RegGetValueA(HKEY_LOCAL_MACHINE, path, "Layout Text", RRF_RT_REG_SZ, NULL, name, &namesz);
	if( err != 0 )  { name[0] = '?'; name[1] = 0; }

	ActivateKeyboardLayout(active_layout, 0);
	return name;
}

static void ShowKeyboardLayouts( void )
{
	HKL layouts [MAX_LAYOUTS];
	int n = GetKeyboardLayoutList(COUNTOF(layouts), layouts);
	if( n == 0 )
	{
		MessageBoxA(NULL, "Failed to get keyboard layouts list", PROG, MB_OK | MB_ICONERROR);
		return;
	}

	UINT_PTR mask = 0;
	for( int i = 0; i < n; ++i )  mask |= (UINT_PTR)layouts[i];

	unsigned width = 0;
	while( mask ) { ++width; mask >>= 4; }

	char output [4096], *po = output;
	size_t remaining_size = COUNTOF(output);

	for( int i = 0; i < n; ++i )
	{
		int len = snprintf(po, remaining_size, "%*llx   %s\n",
		                   width, (UINT_PTR)layouts[i],
		                   GetKeyboardLayoutText(layouts[i]));
		if( len < 0 )  return (void)MessageBoxA(NULL, "Formatting failed (?)", PROG, MB_OK | MB_ICONERROR);
		po += len;
		remaining_size -= len;
	}

	MonospaceBox(PROG, output);
}

// -----------------------------------------------------------------------------

static HWND GlobalGetFocus( void )
{
	HWND fg = GetForegroundWindow();
	if( fg == NULL )  return ERR("GetForegroundWindow"), NULL;

	DWORD fg_thread = GetWindowThreadProcessId(fg, NULL);
	if( fg_thread == 0 )  return ERR("GetWindowThreadProcessId"), NULL;

	GUITHREADINFO gti;
	gti.cbSize = sizeof(gti);
	if( !GetGUIThreadInfo(fg_thread, &gti) )  return ERR("GetGUIThreadInfo"), NULL;

	return gti.hwndFocus;
}

static void SetFocusedWindowLayout( HKL new_layout )
{
	HWND target = GlobalGetFocus(); // TODO: this fails for console windows (GetGUIThreadInfo)
	if( target == NULL )  return;
	PostMessage(target, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)new_layout);
}

// -----------------------------------------------------------------------------

enum
{
	UWM_ACTIVATE_LAYOUT = WM_USER,
};

static const WCHAR kMainWindowClassName [] = L"kbsw.main.6qZK6nb0dYxsgS6H4b8w";
static const WCHAR kHookWindowClassName [] = L"kbsw.hook.6qZK6nb0dYxsgS6H4b8w";

static HWND ghMainWindow = NULL;


HWND AppHookCreateMessageWindow( WNDPROC wndproc )
{
	return CreateMessageWindow(kHookWindowClassName, wndproc);
}

void AppHookNotify( HookSwitchId layout, bool any_modifier_pressed )
{
//	LOG("%llx", layout);
	PostMessage(ghMainWindow, UWM_ACTIVATE_LAYOUT, 0, (LPARAM)layout);
}


static LRESULT CALLBACK MainWindowProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	switch( msg )
	{
		case WM_CREATE:
			if( !HookStart() )  return -1;
			break;

		case WM_DESTROY:
			HookShutdown();
			LOG("%p exited", hwnd);
			break;

		case WM_GETTEXT:
			return snwprintf((WCHAR*)lParam, wParam, L"%ls", GetCommandLineW());

		case UWM_ACTIVATE_LAYOUT:
			SetFocusedWindowLayout((HKL)lParam);
			break;
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

	HookConfigure(&opt->keyboard);

	ghMainWindow = CreateMessageWindow(kMainWindowClassName, MainWindowProc);
	if( ghMainWindow == NULL )
		return false;

	if( running )  StopRunningInstance(running);

	int rc = MessageLoop();

	HookShutdown();
	return rc == 0;
}


void ShowRunningInstanceStatus( void )
{
	HWND running = FindRunningInstance();
	if( running )
	{
		char buffer [256] = PROG" is running.\n\nCommand line:\n\n";
		SendMessageA(running, WM_GETTEXT, COUNTOF(buffer) - strlen(buffer), (LPARAM)(buffer + strlen(buffer)));
		MessageBoxA(NULL, buffer, PROG, MB_OK | MB_ICONINFORMATION);
	}
	else
	{
		MessageBoxA(NULL, PROG" is not running.", PROG, MB_OK | MB_ICONINFORMATION);
	}
}


int main( int argc, char* argv[] )
{
	Options opt = { .command = cmdRun };

	if( !DocOptParseCommandLine(&opt, kUsage, argc, argv) && (opt.command != cmdHelp) )
		return 1;

	switch( opt.command )
	{
		case cmdRun:
			if( opt.keyboard.switches[0].vk == 0 )
			{
				MessageBoxA(NULL, "No switches specified on command line.\n"
				                  "Nothing to do.\n\n"
				                  "Run '"PROG" --help' for a brief usage description.",
				            PROG, MB_OK | MB_ICONERROR);
				return 1;
			}
			if( !Run(&opt) )
			{
				MessageBoxA(NULL, "Something went wrong.\n"PROG" failed to start.", PROG, MB_OK | MB_ICONERROR);
				return 1;
			}
			return 0;

		case cmdQuit:
			return StopRunningInstance(NULL) ? 0 : 1;

		case cmdListLayouts:
			ShowKeyboardLayouts();
			return 0;

		case cmdShowStatus:
			ShowRunningInstanceStatus();
			return 0;

		case cmdHelp:
			MonospaceBox(PROG" "PROG_VERSION, kUsage);
			return 0;
	}

	return -1;
}
