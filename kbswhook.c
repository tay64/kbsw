#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <windows.h>
#include <process.h>
#include "kbswhook.h"
#include "common.h"

// events coming faster are assumed to be injected
#define MIN_DELAY_MS               10

#define NONE                       -1
#define COUNT_ACTIVATE              4  // on which transition count value to activate
#define COUNT_OFF_UP                8  // this sequence should be ignored (switch is up)
#define COUNT_OFF_DOWN              9  // this sequence should be ignored (switch is down)
#define ISDOWN( transition_count )  (transition_count & 1)
#define ISUP( transition_count )    !ISDOWN(transition_count & 1)

// config
static unsigned      gConfTapTimeout_ms;
static VKEY          gConfVKeys [HOOK_MAX_SWITCHES];
static HookSwitchId  gConfIds   [HOOK_MAX_SWITCHES];

// state
static int      gCurrentSwitch = NONE;	// index in gConfVKeys[]
static DWORD    gLastPressTime_ms;
static unsigned gTransitionCount;   // counts both presses and releases; odd = switch is down

// -----------------------------------------------------------------------------

static const VKEY kModifierKeys [] =
{
	VK_LSHIFT,
	VK_LCONTROL,
	VK_LMENU,
	VK_LWIN,
	VK_RSHIFT,
	VK_RCONTROL,
	VK_RMENU,
	VK_RWIN,
};

static void SwitchActivate( unsigned sw )
{
	bool any_modifier_pressed = false;
	for( unsigned i = 0; i < COUNTOF(kModifierKeys); ++i )
	{
		if( kModifierKeys[i] == gConfVKeys[sw] )  continue;
		if( GetAsyncKeyState(kModifierKeys[i]) & 0x8000 )
		{
			any_modifier_pressed = true;
			break;
		}
	}

	AppHookNotify(gConfIds[sw], any_modifier_pressed);
}

// -----------------------------------------------------------------------------

static void SwitchDown( unsigned sw, DWORD timestamp_ms )
{
	DWORD elapsed_ms = timestamp_ms - gLastPressTime_ms;

	gLastPressTime_ms = timestamp_ms;

	if( (sw != gCurrentSwitch) || (elapsed_ms > gConfTapTimeout_ms) )
	{
		// could be a new double-press sequence
		gCurrentSwitch = sw;
		gTransitionCount = 1;
		return;
	}

	if( ISDOWN(gTransitionCount) || (elapsed_ms <= MIN_DELAY_MS) )
	{
		// must be an autorepeat or an injected keypress
		gTransitionCount = COUNT_OFF_DOWN;
		return;
	}

	++gTransitionCount;
}

static void SwitchUp( unsigned sw, DWORD timestamp_ms )
{
	if( sw != gCurrentSwitch )
	{
		gCurrentSwitch = NONE;
		return;
	}

	DWORD elapsed_ms = timestamp_ms - gLastPressTime_ms;

	if( ISUP(gTransitionCount) || (elapsed_ms <= MIN_DELAY_MS) || (elapsed_ms > gConfTapTimeout_ms) )
	{
		gTransitionCount = COUNT_OFF_UP;
		return;
	}

	++gTransitionCount;

	if( gTransitionCount == COUNT_ACTIVATE )
	{
		SwitchActivate(sw);
	}
}


static LRESULT CALLBACK LowLevelKeyboardHook( int code, WPARAM wParam, LPARAM lParam )
{
	if( code == HC_ACTION )
	{
		const KBDLLHOOKSTRUCT* ev = (KBDLLHOOKSTRUCT*)lParam;

		if( (ev->flags & LLKHF_INJECTED) == 0 )
		{
			VKEY vk = ev->vkCode;
			for( unsigned i = 0; i < HOOK_MAX_SWITCHES; ++i )
			{
				if( vk == gConfVKeys[i] )
				{
					((ev->flags & LLKHF_UP) ? SwitchUp : SwitchDown)(i, ev->time);
					return CallNextHookEx(NULL, code, wParam, lParam);
				}
				else if( gConfVKeys[i] == 0 )
				{
					break;
				}
			}
		}

		gCurrentSwitch = NONE;
	}
	return CallNextHookEx(NULL, code, wParam, lParam);
}

// -----------------------------------------------------------------------------

enum
{
	UWM_REPORT_READINESS = WM_USER,
};

static HHOOK ghHook = NULL;

static bool HookInstall( void )
{
	if( ghHook == NULL )
		ghHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardHook, GetModuleHandle(NULL), 0);
	return !!ghHook;
}

static void HookUninstall( void )
{
	if( ghHook != NULL )
		UnhookWindowsHookEx(ghHook);
	ghHook = NULL;
}

static LRESULT CALLBACK HookWindowProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	switch( msg )
	{
		case WM_CREATE:
			if( !HookInstall() ) return ERR("HookInstall"), -1;
			break;

		case WM_DESTROY:
			HookUninstall();
			break;

		case UWM_REPORT_READINESS:
			SetEvent((HANDLE)lParam);
			break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------

static HWND ghHookWindow = NULL;

static void HookThread( void* ready_evt )
{
	ghHookWindow = AppHookCreateMessageWindow(HookWindowProc);
	if( ghHookWindow == NULL )  return;
	PostMessageW(ghHookWindow, UWM_REPORT_READINESS, 0, (LPARAM)ready_evt);
	MessageLoop();
	HookUninstall();
}

bool HookStart( void )
{
	HANDLE thread_ready_evt = CreateEvent(NULL, FALSE, FALSE, NULL);
	if( thread_ready_evt == NULL )
		return ERR("CreateEvent"), false;

	bool ok = true;

	if( _beginthread(HookThread, 0, (void*)thread_ready_evt) == -1 )
		ok = (ERR("_beginthread"), false);

	if( ok )
	{
		if( WaitForSingleObject(thread_ready_evt, 100) != 0 )
			ok = (ERR("WaitForSingleObject"), false);
	}

	CloseHandle(thread_ready_evt);
	return ok;
}

void HookShutdown( void )
{
	if( ghHookWindow )
	{
		SendMessageW(ghHookWindow, WM_CLOSE, 0, 0);
		ghHookWindow = NULL;
	}
}

void HookConfigure( const HookParameters* hp )
{
	for( unsigned i = 0; i < HOOK_MAX_SWITCHES; ++i )
	{
		gConfVKeys[i] = hp->switches[i].vk;
		gConfIds[i]   = hp->switches[i].id;
	}
	gConfTapTimeout_ms = hp->tap_timeout_ms;
}