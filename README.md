# kbsw
A simple non-modal Windows keyboard layout switcher controlled by double-taps of modifier keys.

## Features

- Non-modal: pressing a layout switch key always activates the same keyboard layout. No more mistaking which layout is currently active!

- Easy to use keyboard shortcuts: just double-tap a modifier key (any of the <kbd>Ctrl</kbd>, <kbd>Alt</kbd>, <kbd>Shift</kbd>, <kbd>Win</kbd>;
right and left keys are distinct). Mode keys are also supported (<kbd>CapsLock</kbd>, <kbd>NumLock</kbd>, <kbd>ScrollLock</kbd>).

- Unlike some other keyboard switching tools, works with Console windows.

- Fix garbled text resulting from typing in a wrong keyboard layout: select it and activate the desired layout
while holding down any modifier key.

- Translate hexadecimal Unicode codepoints to characters, or vice versa: `U+0061 U+1F4A1 U+0021` → `a 💡 !`,
or `3.2.1.💥!` → `3=U+33 .=U+2E 2=U+32 .=U+2E 1=U+31 .=U+2E 💥=U+1F4A5 !=U+21 `.

- Automatically pauses in games, so as not to interfere with your controls. There is an option to disable this behavior.

- No UI: just put a shortcut to `kbsw` with appropriate parameters into your *Programs/Startup* menu folder.
If needed, you can interact with the running instance from command line.


## Limitations

- No UI: a regular Windows user is unaccustomed to command-line programs and it can be inconvenient for them.

- Won't work with elevated apps, unless you arrange for `kbsw` itself to run elevated.

- The selected text translation works by simulating keyboard Copy&Paste commands
(<kbd>Ctrl+C</kbd>/<kbd>Ctrl+V</kbd>, or sometimes <kbd>Ctrl+INSERT</kbd>/<kbd>Shift+INSERT</kbd>).
It replaces the content of the clipboard, and can work incorrectly with some applications.
Unfortunately this is the only more-or-less universal method available on Windows; all the alternatives are more limited.

- As a consequence of the above, the selected text translation is somewhat awkward in Console windows: it fails to replace the selection
with its translation, instead pasting the translation alongside the original text.

## Usage

```
Command line: kbsw [options] KEY[=LAYOUT] [KEY[=LAYOUT]...]

where KEY can be one of the following:
    LC  LCtrl   LeftCtrl   LeftControl
    RC  RCtrl   RightCtrl  RightControl
    LS  LShift  LeftShift
    RS  RShift  RightShift
    LA  LAlt    LeftAlt
    RA  RAlt    RightAlt
    LW  LWin    LeftWin
    RW  RWin    RightWin
    CL  Caps    CapsLock
    NL          NumLock
    SL          ScrollLock

and LAYOUT codes can be obtained by running
    kbsw --list-layouts

A special dummy layout named 'HEX' can be used for Hexadecimal<->Unicode
conversion (see Usage below).

You can omit '=LAYOUT' for some or all KEYs; these layouts will be assigned
automatically in the order they appear in --list-layouts.

-t --timeout=300   KEY double-press timeout, in milliseconds
-q --quiet         suppress error messages (only return error code)
-F --fullscreen    do not ignore fullscreen apps
-x --exit          stop the running copy of kbsw
-p --pause         make the running instance stop doing anything
-r --resume        make a paused running instance resume working
-s --status        show parameters of the running instance
-l --list-layouts  display installed keyboard layouts
-h --help          show this text

Usage:

 - Press KEY twice quickly to switch to the corresponding keyboard LAYOUT.

 - To correct some text mistakenly typed in a wrong keyboard layout,
   select it and press the correct layout's KEY quickly twice while
   holding down any other modifier key (such as Shift, Alt, Ctrl).
   This action replaces the clipboard content.

 - To convert hexadecimal Unicode codepoint(s) into character(s),
   for example 'U+0040' to '@', select them and double-tap a KEY
   assigned to the special LAYOUT named 'HEX'.

 - To do the reverse of the above, select some characters and double-tap a KEY
   assigned to a special LAYOUT 'HEX' while holding down any other modifier key.
```

## Building

See the comment at the top of the file `src/kbsw.c`.
