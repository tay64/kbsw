// MINGW64:
// gcc -std=c11 -Wall -Werror -mwindows -O2 -flto -o kbsw.exe kbsw.c docopt.c
//     -DKBSW_STDOUT    enable logging to stdout (run from mintty to see it)

#define PROG "kbsw"

// TODO: use a custom window to diplay this text instead of MessageBox
// and get rid of the unreliable lazy formatting with spaces and tabs
const char kUsage [] =
	"Usage: "PROG" [options] KEY=LAYOUT [KEY=LAYOUT...]\n"
	"\n"
	"where LAYOUT codes can be obtained by running\n"
	"	"PROG" --list-layouts\n"
	"\n"
	"and KEY can be one of the following:\n"
	"	LC	LCtrl	LeftCtrl		LeftControl\n"		// VK_LCONTROL
	"	RC	RCtrl	RightCtrl		RightControl\n"		// VK_RCONTROL
	"	LS	LShift	LeftShift\n"						// VK_LSHIFT
	"	RS	RShift	RightShift\n"						// VK_RSHIFT
	"	LA	LAlt	LeftAlt\n"							// VK_LMENU
	"	RA	RAlt	RightAlt\n"							// VK_RMENU
	"	LW	LWin	LeftWin\n"							// VK_LWIN
	"	RW	RWin	RightWin\n"							// VK_RWIN
	"\n"
	"-t --timeout=300	double-tap timeout, in milliseconds\n"
	"-q --quit      	stop the running copy of "PROG"\n"
	"-l --list-layouts	display installed keyboard layouts and exit\n"
	"-h --help      	show this text and exit\n"
	;

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <windows.h>
#include "docopt.h"
#include "kbswhook.h"
#include "common.h"
#include "version.h"

// in the order of rising precedence: e.g. if Quit and Help are both specified, Help has effect
typedef enum
{
	cmdRun,
	cmdListLayouts,
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

	const char* kw = DocOptFindLineWithWord(kUsage, "\n	", keyname);
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
		case 'q':  cmd = cmdQuit; break;
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


static void PrintKeyboardlayouts(void) {
	HKL kl[8];
	int n = GetKeyboardLayoutList(COUNTOF(kl), kl);
	LOG("active: %8x", (unsigned)(UINT_PTR)GetKeyboardLayout(GetCurrentThreadId()));
	for( int i = 0; i < n; ++i ) {
		char tag [KL_NAMELENGTH];
		if( !GetKeyboardLayoutNameA(tag) )  tag[0] = 0;

		//  Layout Display Name
		//  SHLoadIndirectString

		WCHAR name [256], path[256];
		DWORD namesz = sizeof(name);
		snwprintf(path, COUNTOF(path), L"%hs\\%hs", "SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", tag);
		int rc = RegGetValueW(HKEY_LOCAL_MACHINE, path, L"Layout Text",
		                      RRF_RT_REG_SZ, NULL, name, &namesz);
		if( rc != 0 )  { name[0] = '?'; name[1] = 0; }
		LOG("[%d]: %8x  %ls", i, (unsigned)(UINT_PTR)GetKeyboardLayout(GetCurrentThreadId()), name);
		ActivateKeyboardLayout((HKL)HKL_NEXT, 0);
	}
	LOG("active: %8x", (unsigned)(UINT_PTR)GetKeyboardLayout(GetCurrentThreadId()));
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
			break;

		case UWM_ACTIVATE_LAYOUT:
			SetFocusedWindowLayout((HKL)lParam);
			break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------

static bool Run( const Options* opt )
{
	HookConfigure(&opt->keyboard);

	ghMainWindow = CreateMessageWindow(kMainWindowClassName, MainWindowProc);
	if( ghMainWindow == NULL )
		return false;

	int rc = MessageLoop();

	HookShutdown();
	return rc == 0;
}

static HWND FindRunningInstance( void )
{
	return FindWindowW(kMainWindowClassName, NULL);
}

static bool StopRunningInstance( void )
{
	HWND running = FindRunningInstance();
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


int main( int argc, char* argv[] )
{
	Options opt = { .command = cmdRun };

	if( !DocOptParseCommandLine(&opt, kUsage, argc, argv) && (opt.command != cmdHelp) )
		return 1;

	switch( opt.command )
	{
		case cmdRun:
			if( !Run(&opt) )
			{
				MessageBoxA(NULL, "Something went wrong.\n"PROG" failed to start.", PROG, MB_OK | MB_ICONERROR);
				return 1;
			}
			return 0;

		case cmdQuit:
			return StopRunningInstance() ? 0 : 1;

		case cmdListLayouts:
			PrintKeyboardlayouts();
			return 0;

		case cmdHelp:
			MessageBoxA(NULL, kUsage, PROG" "PROG_VERSION, MB_OK | MB_ICONINFORMATION);
			return 0;
	}

	return -1;
}
