#ifndef COMMON_H
#define COMMON_H

#if defined(KBSW_STDOUT)
	#include <stdio.h>
	#define PRINT(fmt, ...)  (void)(printf(fmt, ##__VA_ARGS__), fflush(stdout))
#else
	#define PRINT(fmt, ...)  ((void)0)
#endif

#define LOG(fmt, ...)    PRINT("%s:%d: "fmt"\n", __func__, __LINE__, ##__VA_ARGS__)
#define ERR(fmt, ...)    LOG(fmt" error %lu", ##__VA_ARGS__, GetLastError())

#define COUNTOF(a)       (sizeof(a) / sizeof((a)[0]))

typedef unsigned VKEY; // for VK_xxx

enum { MAX_KEYBOARD_LAYOUTS = 8 };

extern const VKEY kModifierVKeys []; // terminated with a 0

#endif
