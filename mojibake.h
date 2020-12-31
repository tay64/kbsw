#ifndef MOJIBAKE_H
#define MOJIBAKE_H

#include <stdbool.h>
#include <windows.h>

// ---- provided by mojibake.c -------------------------------------------------

// All these functions must be called from the same thread

bool MojibakeIsBusy( void );

// will start a thread-associated timer (SetTimer) and complete asynchronously
void MojibakeTranslateSelection( HWND hwnd_target, HKL target_layout );

// The app should register with AddClipboardFormatListener and call this fn on WM_CLIPBOARDUPDATE.
// `worker_hwnd` is only used as a nominal clipboard data owner when copying/pasting.
void MojibakeOnClipboardUpdate( HWND worker_hwnd );

#endif
