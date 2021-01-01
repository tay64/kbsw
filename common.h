#ifndef COMMON_H
#define COMMON_H

#if defined(KBSW_STDOUT)
	#include <stdio.h>
	#define LOG(fmt, ...) (void)(printf("%s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__), fflush(stdout))
#else
	#define LOG(...) ((void)0)
#endif

#define ERR(msg)  LOG("%s error %lu", msg, GetLastError())

#define COUNTOF(a)  (sizeof(a) / sizeof((a)[0]))

#endif
