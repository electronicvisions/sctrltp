#pragma once

#include "logger/syslog/logger.h"
#include <cstdio>

#ifdef SYSLOG_THRESHOLD
#define _SYSLOG_THRESHOLD_PREDEFINED
#else
#define SYSLOG_THRESHOLD logger::LogPriority::INFO
#endif

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
		char buffer[256];                                                                          \
		snprintf(buffer, sizeof(buffer), __VA_ARGS__);                                             \
		LOGGER_OPEN_SYSLOG_CLOSE(ERROR, "sctrltp", buffer);                                        \
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

#ifdef _SYSLOG_THRESHOLD_PREDEFINED
#undef _SYSLOG_THRESHOLD_PREDEFINED
#else
#undef SYSLOG_THRESHOLD
#endif