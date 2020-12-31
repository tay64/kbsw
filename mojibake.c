#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <windows.h>
#include "mojibake.h"
#include "common.h"

enum
{
	WMCOPY_TIMEOUT_ms = 100,
	CTRL_INSERT_TIMEOUT_ms = 300,
	PASTE_DELAY_ms = 100,  // for some reason, sometimes paste right after SetKeyboardData doesn't work (observed in Far)
};

typedef enum
{
	sIdle,
	sWaitingForWmCopy,
	sWaitingForKeyboardCopy,
	sDelayBeforePaste,
} State;

typedef enum
{
	shNoSpecialHandling,
	shIgnore,
	shCtrlInsert,
} SpecialHandling;


static State            gState = sIdle;
static HWND             ghWndTarget;
static HKL              ghTargetLayout;
static DWORD            gStartTime_ms;
static UINT_PTR         gTimer;
static SpecialHandling  gSpecialHandling;


// -----------------------------------------------------------------------------

static const struct { const char* exe; SpecialHandling sh; } kExeSpecialHandling [] =
{
	{ "putty.exe",  shIgnore },
	{ "kitty.exe",  shIgnore },
	{ "mintty.exe", shCtrlInsert },
};

static SpecialHandling GetWindowSpecialHandling( HWND hwnd )
{
	DWORD pid;
	GetWindowThreadProcessId(hwnd, &pid);
	HANDLE hprocess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if( hprocess == NULL )  return ERR("OpenProcess"), shNoSpecialHandling;

	char exepath [1024];
	DWORD plen = COUNTOF(exepath);
	if( !QueryFullProcessImageNameA(hprocess, 0, exepath, &plen) )
		return ERR("QueryFullProcessImageName"), CloseHandle(hprocess), shNoSpecialHandling;
	CloseHandle(hprocess);

	const char* exe = exepath + plen;
	while( (exe != exepath) && (*exe != '/') && (*exe != '\\') ) --exe;
	if( exe != exepath ) ++exe;
	LOG("exe: %s", exe);

	// check the list of exceptional exe names
	for( unsigned i = 0; i < COUNTOF(kExeSpecialHandling); ++i )
	{
		if( stricmp(exe, kExeSpecialHandling[i].exe) == 0 )
			return kExeSpecialHandling[i].sh;
	}

	// a special rule for classical console windows
	char classname [64];
	if( GetClassNameA(hwnd, classname, COUNTOF(classname)) )
	{
		if( strcmp(classname, "ConsoleWindowClass") == 0 )
			return shCtrlInsert;
	}
	else ERR("GetClassName");

	return shNoSpecialHandling;
}

// -----------------------------------------------------------------------------

#define VKS_NO_MAPPING       -1
#define VKS_SHIFT            0x100
#define VKS_CTRL             0x200
#define VKS_ALT              0x400
#define VKS_VKEY(vkmod)      LOBYTE(vkmod)

#define TUE_NOGLOBALKBSTATE  2

// Not thread-safe: remove 'static' from 'keystate' declaration to fix.
// Writes the resulting character(s) into *ppout, advances *ppout and decreases
// *pout_remaining_cch by the number of characters written.
// Returns the number of characters it has written (0 if no translation exists).
static unsigned TranslateChar( WCHAR ch, WCHAR** ppout, size_t* pout_remaining_cch,
                               HKL source_layout, HKL target_layout )
{
	SHORT vkmod = VkKeyScanExW(ch, source_layout);
	if( vkmod == VKS_NO_MAPPING )  return LOG("VkKeyScanExW: cannot map '%lc'", ch), 0;

	static BYTE keystate [256] = {0};
	keystate[VK_SHIFT]   = (vkmod & VKS_SHIFT) ? 0x80 : 0;
	keystate[VK_CONTROL] = (vkmod & VKS_CTRL ) ? 0x80 : 0;
	keystate[VKS_ALT]    = (vkmod & VKS_ALT  ) ? 0x80 : 0;

	int rc = ToUnicodeEx(VKS_VKEY(vkmod), 0, keystate, *ppout, *pout_remaining_cch,
	                     TUE_NOGLOBALKBSTATE, target_layout);

	if( rc > 0 )
	{
		*ppout += rc;
		*pout_remaining_cch -= rc;
		return rc;
	}

	LOG("ToUnicodeExW: cannot map '%lc'", ch);
	return 0;
}

static HGLOBAL TranslateString( const WCHAR* source_text, HKL source_layout, HKL target_layout )
{
	size_t source_cch = wcslen(source_text);
	size_t target_cch = source_cch * 2;  // double in case target layout produces more UTF16 characters

	HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, (target_cch + 1) * sizeof(WCHAR));
	if( hmem == NULL )  return ERR("GlobalAlloc"), NULL;

	WCHAR* output_text = (WCHAR*) GlobalLock(hmem);
	if( output_text == NULL )  return ERR("GlobalLock"), GlobalFree(hmem), NULL;

	WCHAR* dst = output_text;
	for( size_t i = 0; i < source_cch; ++i )
	{
		unsigned n = TranslateChar(source_text[i], &dst, &target_cch, source_layout, target_layout);
		if( (n == 0) && (target_cch > 0) )
		{
			*dst++ = source_text[i];
			--target_cch;
		}
	}
	*dst = 0;

	LOG("[%ls]", output_text);

	GlobalUnlock(hmem);
	return hmem;
}

// returns a (unnormalized) score of how likely it is that `str` was typed in `layout`
static size_t MatchStringToLayout( const WCHAR* str, HKL layout )
{
	size_t score = 0;
	for( WCHAR ch; (ch = *str) != 0; ++str )
	{
		score += (VkKeyScanExW(ch, layout) != VKS_NO_MAPPING);
	}
	return score;
}

// returns a keyboard layout most likely `str` was typed in, or NULL if cannot detect
static HKL DetectStringLayout( const WCHAR* str, HKL preferred_layout )
{
	HKL best_layout = 0;
	size_t best_score = 0;
	HKL layouts [MAX_KEYBOARD_LAYOUTS];
	int n = GetKeyboardLayoutList(COUNTOF(layouts), layouts);
	for( int i = 0; i < n; ++i )
	{
		size_t score = MatchStringToLayout(str, layouts[i]);
		if( (score > best_score) || ((score == best_score) && score && (best_layout != preferred_layout)) )
		{
			best_score = score;
			best_layout = layouts[i];
		}
	}
	LOG("%llx (score %llu/%llu)", (UINT_PTR)best_layout, best_score, wcslen(str));
	return best_score ? best_layout : NULL;
}

// `worker_hwnd` is only used as a nominal clipboard data owner
static bool TranslateClipboard( HKL target_layout, HWND worker_hwnd )
{
	bool done = false;
	HANDLE hcd = NULL;
	const WCHAR* txt = NULL;
	HGLOBAL hmem_translated = NULL;

	if( !OpenClipboard(worker_hwnd) )
	{
		ERR("OpenClipboard");
		goto cleanup;
	}

	hcd = GetClipboardData(CF_UNICODETEXT);
	if( hcd == NULL )
	{
		ERR("GetClipboardData");
		goto cleanup;
	}

	txt = (WCHAR*)GlobalLock(hcd);
	if( txt == NULL )
	{
		ERR("GlobalLock");
		goto cleanup;
	}

	HKL source_layout = DetectStringLayout(txt, target_layout);
	LOG("clip [%.60ls] %llx->%llx", txt, (UINT_PTR)source_layout, (UINT_PTR)target_layout);
	if( source_layout == target_layout )
	{
		LOG("noop");
		goto cleanup;
	}

	hmem_translated = TranslateString(txt, source_layout, target_layout);
	if( hmem_translated == NULL )  goto cleanup;

	if( !EmptyClipboard() )
	{
		ERR("EmptyClipboard");
		goto cleanup;
	}

	if( SetClipboardData(CF_UNICODETEXT, hmem_translated) == NULL )
	{
		ERR("SetClipboardData");
		goto cleanup;
	}

	hmem_translated = NULL;  // now the handle is owned by the clipboard
	done = true;

cleanup:
	if( hmem_translated )  GlobalFree(hmem_translated);
	if( txt )  GlobalUnlock(hcd);
	CloseClipboard();
	return done;
}

// -----------------------------------------------------------------------------

static int AddKeypress( INPUT* keypresses, unsigned keypresses_maxcount, int idx,
                        VKEY vk, bool down )
{
	if( (idx < 0 ) || (idx >= keypresses_maxcount) )  return -1;
	INPUT* pi = &keypresses[idx];
	memset(pi, 0, sizeof(*pi));
	pi->type = INPUT_KEYBOARD;
	pi->ki.wVk = vk;
	pi->ki.wScan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
	pi->ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
	if( vk == VK_INSERT )  pi->ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
	return idx + 1;
}

static bool SendKeyChord( VKEY vk_modifier, VKEY vk_key )
{
	INPUT keypresses [32]; // should be large enough for all modifiers held down and our keys
	int nkeys = 0, nmods = 0;

	// unpress any currently pressed modifiers
	for( unsigned i = 0; kModifierVKeys[i] != 0; ++i )
	{
		if( GetAsyncKeyState(kModifierVKeys[i]) & 0x8000 )
		{
			nkeys = AddKeypress(keypresses, COUNTOF(keypresses), nkeys, kModifierVKeys[i], false);
			++nmods;
		}
	}

	// press and release the requested chord
	nkeys = AddKeypress(keypresses, COUNTOF(keypresses), nkeys, vk_modifier, true);
	nkeys = AddKeypress(keypresses, COUNTOF(keypresses), nkeys, vk_key,      true);
	nkeys = AddKeypress(keypresses, COUNTOF(keypresses), nkeys, vk_key,      false);
	nkeys = AddKeypress(keypresses, COUNTOF(keypresses), nkeys, vk_modifier, false);

	// restore the original modifiers
	for( unsigned i = 0; i < nmods; ++i )
	{
		nkeys = AddKeypress(keypresses, COUNTOF(keypresses), nkeys, keypresses[i].ki.wVk, true);
	}

	return (nkeys > 0) ? SendInput(nkeys, keypresses, sizeof(keypresses[0])) : false;
}

static bool SimulateKeyboardCopy( SpecialHandling sh )
{
	switch( sh )
	{
		case shIgnore:             return false;
		case shCtrlInsert:         return LOG("Ctrl+INS"), SendKeyChord(VK_CONTROL, VK_INSERT);
		case shNoSpecialHandling:  return LOG("Ctrl+C"),   SendKeyChord(VK_CONTROL, 'C');
	}
	return false;
}

static bool SimulateKeyboardPaste( SpecialHandling sh )
{
	switch( sh )
	{
		case shIgnore:             return false;
		case shCtrlInsert:         return LOG("Shift+INS"), SendKeyChord(VK_SHIFT, VK_INSERT);
		case shNoSpecialHandling:  return LOG("Ctrl+V"),    SendKeyChord(VK_CONTROL, 'V');
	}
	return false;
}

// -----------------------------------------------------------------------------

static void MojibakeTimer( HWND _hwnd, UINT _msg, UINT_PTR _id, DWORD current_time_ms )
{
	DWORD elapsed_ms = current_time_ms - gStartTime_ms;

	switch( gState )
	{
		case sWaitingForWmCopy:
			if( elapsed_ms >= WMCOPY_TIMEOUT_ms )
			{
				LOG("WM_COPY timed out");
				gStartTime_ms = current_time_ms;
				gState = SimulateKeyboardCopy(gSpecialHandling)
				       ? sWaitingForKeyboardCopy
				       : (KillTimer(NULL, gTimer), sIdle);
			}
			break;

		case sWaitingForKeyboardCopy:
			if( elapsed_ms >= CTRL_INSERT_TIMEOUT_ms )
			{
				KillTimer(NULL, gTimer);
				gState = sIdle;
				LOG("keyboard copy timed out");
			}
			break;

		case sDelayBeforePaste:
			if( elapsed_ms >= PASTE_DELAY_ms )
			{
				KillTimer(NULL, gTimer);
				gState = sIdle;
				SimulateKeyboardPaste(gSpecialHandling);
			}
			break;

		default:
			LOG("turning off timer in state %d (elapsed %ldms)", gState, elapsed_ms);
			KillTimer(NULL, gTimer);
	}
}

// -----------------------------------------------------------------------------

void MojibakeOnClipboardUpdate( HWND worker_hwnd )
{
	if( (gState == sWaitingForWmCopy) || (gState == sWaitingForKeyboardCopy) )
	{
		if( TranslateClipboard(ghTargetLayout, worker_hwnd) )
		{
			gStartTime_ms = GetTickCount();
			gState = sDelayBeforePaste;
		}
	}
}


// correct only if all Mojibake* functions are called from within the same thread
bool MojibakeIsBusy( void )
{
	return (gState != sIdle);
}


void MojibakeTranslateSelection( HWND hwnd_target, HKL target_layout )
{
	if( MojibakeIsBusy() )  return LOG("busy");

	gSpecialHandling = GetWindowSpecialHandling(hwnd_target);
	if( gSpecialHandling == shIgnore )  return LOG("ignored (special handling)");

	ghWndTarget    = hwnd_target;
	ghTargetLayout = target_layout;
	gStartTime_ms  = GetTickCount();
	gTimer         = SetTimer(NULL, 0, 10, MojibakeTimer);

	if( gTimer == 0 )  return ERR("SetTimer");

	gState = sWaitingForWmCopy;
	PostMessage(hwnd_target, WM_COPY, 0, 0);
	LOG("sent WM_COPY");
}
