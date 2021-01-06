#ifndef MOJIBAKE_H
#define MOJIBAKE_H

#include <stdbool.h>
#include <windows.h>

#define HKL_AUTOASSIGN      ((HKL)-1)
#define HKL_HEX_TO_UNICODE  ((HKL)-2)
#define HKL_UNICODE_TO_HEX  ((HKL)-3)


// ---- provided by mojibake.c -------------------------------------------------

// All these functions must be called from the same thread

bool MojibakeIsBusy( void );

// Translate current selection in the window `hwnd_target` into `target_layout`.
// `target_layout` can also be HKL_HEX_TO_UNICODE or HKL_UNICODE_TO_HEX.
// Will start a thread-associated timer (SetTimer) and complete asynchronously.
void MojibakeTranslateSelection( HWND hwnd_target, HKL target_layout );

// The app should register with AddClipboardFormatListener and call this fn on WM_CLIPBOARDUPDATE.
// `worker_hwnd` is only used as a nominal clipboard data owner when copying/pasting.
void MojibakeOnClipboardUpdate( HWND worker_hwnd );

#endif
