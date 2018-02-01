#pragma once

#ifndef htobe64
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htobe64(x) __bswap_64(x)
#define htole64(x) (x)
#define be64toh(x) __bswap_64(x)
#define le64toh(x) (x)
#else
// ECM: replace next line by warning if you really want to test it...
#error This simulator simulates software with big endianness. Untested!
#define htobe64(x) (x)
#define htole64(x) __bswap_64(x)
#define be64toh(x) (x)
#define le64toh(x) __bswap_64(x)
#endif
#endif

/* ack-only packet type: without payload */
struct ack_frame
{
	sctrltp::packet::seq_t ack;
} __attribute__((__packed__));


/* next_seq: calculate next sequence number (incl. wrap around at SEQ_SIZE) */
inline sctrltp::packet::seq_t next_seq(sctrltp::packet::seq_t s) __attribute__((always_inline));
sctrltp::packet::seq_t next_seq(sctrltp::packet::seq_t s)
{
#if SEQ_SIZE > UINT_MAX
	return s + 1;
#else
	return (s + 1) % SEQ_SIZE;
#endif
}


/* in_window: check if sequence number x is in window (interval [a, b]) */
inline bool in_window(sctrltp::packet::seq_t x, sctrltp::packet::seq_t a, sctrltp::packet::seq_t b)
    __attribute__((always_inline));
bool in_window(sctrltp::packet::seq_t x, sctrltp::packet::seq_t a, sctrltp::packet::seq_t b)
{
	sctrltp::packet::seq_t bc = b % SEQ_SIZE; // constrained seq numbers
	if (a == bc) {                            // window empty
		return false;
	} else if (a < bc) { // typical
		bool ret = (a < x) && (x <= bc);
		return ret;
	} else { // wrapped around
		bool ret = (a < x) || (x <= bc);
		return ret;
	}
}


/* dist_window: calculate distance between start of window a and sequence number x */
size_t dist_window(sctrltp::packet::seq_t x, sctrltp::packet::seq_t a)
{
	size_t ret = 0;
	if (a < x)
		ret = x - a;
	else // x wrapped around
		ret = x + (SEQ_SIZE - a);
	return ret;
}


///* timeout: sleeps */
void sleep_ns(uint64_t ns)
{
	assert(ns > 1000);
}


/* mytime: measure current time (nanoseconds since 1970) */
inline uint64_t mytime() __attribute__((always_inline));
uint64_t mytime()
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1E9 + t.tv_nsec;
}
