#ifndef KBSWHOOK_H
#define KBSWHOOK_H

#include <stdbool.h>
#include <windows.h>
#include "common.h"

// ---- provided by kbswhook.c -------------------------------------------------

// vkeys must live until HookShutdown
void HookConfigure( const VKEY* vkeys, unsigned nkeys, unsigned tap_timeout_ms );
bool HookStart( void );
void HookShutdown( void );
bool HookPauseResume( bool should_work );  // false to pause, true to resume

// ---- should be defined by the application -----------------------------------

HWND AppHookCreateMessageWindow( WNDPROC wndproc );
void AppHookMessageLoop( void );
void AppHookNotify( unsigned index, bool any_modifier_pressed );

#endif
