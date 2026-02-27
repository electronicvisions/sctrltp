#pragma once

#include <cstdio>

#ifndef LOGLEVEL
// Let's spam the user if he doesn't define it ;)
#define LOGLEVEL 9
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#define SCTRL_LOG_ERROR(...)                                                                       \
	do {                                                                                           \
		fprintf(stderr, "ERROR %s:%d: ", __FILE__, __LINE__);                                      \
		fprintf(stderr, __VA_ARGS__);                                                              \
		fprintf(stderr, "\n");                                                                     \
	} while (0)
#if LOGLEVEL > 0
#define SCTRL_LOG_WARN(...)                                                                        \
	do {                                                                                           \
		fprintf(stderr, "WARN  %s:%d: ", __FILE__, __LINE__);                                      \
		fprintf(stderr, __VA_ARGS__);                                                              \
		fprintf(stderr, "\n");                                                                     \
	} while (0)
#else
#define SCTRL_LOG_WARN(...)                                                                        \
	do {                                                                                           \
	} while (0)
#endif
#if LOGLEVEL > 1
#define SCTRL_LOG_INFO(...)                                                                        \
	do {                                                                                           \
		fprintf(stderr, "INFO  %s:%d: ", __FILE__, __LINE__);                                      \
		fprintf(stderr, __VA_ARGS__);                                                              \
		fprintf(stderr, "\n");                                                                     \
	} while (0)
#else
#define SCTRL_LOG_INFO(...)                                                                        \
	do {                                                                                           \
	} while (0)
#endif
#if LOGLEVEL > 2
#define SCTRL_LOG_DEBUG(...)                                                                       \
	do {                                                                                           \
		fprintf(stderr, "DEBUG %s:%d: ", __FILE__, __LINE__);                                      \
		fprintf(stderr, __VA_ARGS__);                                                              \
		fprintf(stderr, "\n");                                                                     \
	} while (0)
#else
#define SCTRL_LOG_DEBUG(...)                                                                       \
	do {                                                                                           \
	} while (0)
#endif
#pragma GCC diagnostic pop