#ifndef KBSWHOOK_H
#define KBSWHOOK_H

#include <stdbool.h>
#include <windows.h>
#include "common.h"

enum { HOOK_MAX_SWITCHES = 8 };

typedef UINT_PTR HookSwitchId;

typedef struct
{
	VKEY         vk;
	HookSwitchId id;    // user-defined value (for AppHookNotify)
} HookSwitchDef;

typedef struct
{
	HookSwitchDef switches [HOOK_MAX_SWITCHES];
	unsigned      tap_timeout_ms;
} HookParameters;

// ---- provided by kbswhook.c -------------------------------------------------

void HookConfigure( const HookParameters* hp );
bool HookStart( void );
void HookShutdown( void );

// ---- should be defined by the application -----------------------------------

HWND AppHookCreateMessageWindow( WNDPROC wndproc );
void AppHookNotify( HookSwitchId id, bool any_modifier_pressed );

#endif
