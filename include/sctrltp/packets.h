#pragma once

/** \brief Headerfile for Packethandling in SCTP
 *	Implements some short but necessary funcs to de/encapsulate packetcontents
*/


#include <linux/types.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stddef.h>

#include "sctrltp_defines.h"

#ifndef LOGLEVEL
// Let's spam the user if he doesn't define it ;)
#define LOGLEVEL 9
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#define LOG_ERROR(...) do { \
	fprintf(stderr, "ERROR %s:%d: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); \
	fprintf(stderr, "\n"); \
} while(0)
#if LOGLEVEL > 0
#define LOG_WARN(...) do { \
	fprintf(stderr, "WARN  %s:%d: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); \
	fprintf(stderr, "\n"); \
} while(0)
#else
#define LOG_WARN(...) do { } while(0)
#endif
#if LOGLEVEL > 1
#define LOG_INFO(...) do { \
	fprintf(stderr, "INFO  %s:%d: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); \
	fprintf(stderr, "\n"); \
} while(0)
#else
#define LOG_INFO(...) do { } while(0)
#endif
#if LOGLEVEL > 2
#define LOG_DEBUG(...) do { \
	fprintf(stderr, "DEBUG %s:%d: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); \
	fprintf(stderr, "\n"); \
} while(0)
#else
#define LOG_DEBUG(...) do { } while(0)
#endif
#pragma GCC diagnostic pop

#if (__GNUC__ >= 9)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif

namespace sctrltp {

template<typename P = Parameters<>>
struct arq_frame {
	__u32   ACK;                /*Acknowledge to packet with sequenceno = ACK (other direction)*/
	__u32   SEQ;                /*Sequencenumber*/
	__u16   PTYPE;              /*Type of packet (called "packet id" in docs)*/
	__u16   LEN;                /*Length (64-bit words)*/
	__u64   COMMANDS[P::MAX_PDUWORDS];
}__attribute__ ((packed));
// TODO: also check non-default-parameterized versions
static_assert(offsetof(arq_frame<>, COMMANDS) == (sizeof(__u32)*2 + sizeof(__u16)*2), "");
// TODO: check for uint64_t ptr alignment requirements too!

struct arq_ackframe {
	__u32   ACK;                /*Acknowledge to packet with sequenceno = ACK (other direction)*/
};
static_assert(sizeof(struct arq_ackframe) == 4, "");

struct arq_resetframe {
	uint32_t magic_word;
};
static_assert(sizeof(struct arq_resetframe) == 4, "");


/**** FUNCS USED BY SCTP LAYER ****/

void parse_mac (char *in, __u8 *out);
void print_mac (const char *prefix, __u8 *mac);


/*SCTPREQ_* These funtions are used by the sending side of the layer*/

template<typename AF>
AF* sctpreq_get_ptr (AF* packet);


// TODO: static inline, but C99 style extern inline seems nicer...

template<typename AF>
static inline void sctpreq_set_header (AF* packet, __u16 LEN, __u16 PTYPE) {
	packet->LEN = htons(LEN);
	packet->PTYPE = htons(PTYPE);
}

/*seq valid bit will be set too!!*/
template<typename AF>
__attribute__((always_inline)) static inline void sctpreq_set_seq (AF* packet, __u32 SEQ) {
	packet->SEQ = htonl(SEQ); /* size: 7 bits! */
}

template<typename AF>
__attribute__((always_inline)) static inline void sctpreq_set_ack (AF* packet, __u32 ACK) {
	packet->ACK = htonl(ACK);
}

__attribute__((always_inline)) static inline void sctpack_set_ack (arq_ackframe *packet, __u32 ACK) {
	packet->ACK = htonl(ACK);
}

/*get length in words*/
template<typename AF>
__attribute__((always_inline)) static inline __u16 sctpreq_get_len (AF* packet) {
	return ntohs(packet->LEN);
}

/*get size in bytes*/
template<typename AF>
__attribute__((always_inline)) static inline __u32 sctpreq_get_size (AF* packet) {
	__u32 size = ARQ_HEADER_SIZE;
	size += TYPLEN_SIZE + ntohs(packet->LEN) * WORD_SIZE;
	if (size < MIN_PACKET_SIZE)
		size = MIN_PACKET_SIZE;
	return size;
}

template<typename AF>
__attribute__((always_inline)) static inline __u32 sctpreq_get_seq (AF* packet) {
	__u32 seq;
	seq = ntohl(packet->SEQ);
	return seq;
}

template<typename AF>
__attribute__((always_inline)) static inline __u32 sctpreq_get_ack (AF* packet) {
	return ntohl(packet->ACK);
}

template<typename AF>
__attribute__((always_inline)) static inline __u16 sctpreq_get_typ (AF* packet) {
	return ntohs(packet->PTYPE);
}

template<typename AF>
__attribute__((always_inline)) static inline __u64* sctpreq_get_pload (AF* packet) {
	return packet->COMMANDS;
}

static inline void sctpreset_init (struct arq_resetframe *packet) {
	packet->magic_word = htonl(HW_HOSTARQ_MAGICWORD);
}


template<typename AF>
__attribute__((always_inline)) static inline __u32 sctpsomething_get_size (AF* packet, size_t nread) {
	__u32 size = ARQ_HEADER_SIZE;
	if (nread <= MIN_PACKET_SIZE)
		return MIN_PACKET_SIZE;
	size += TYPLEN_SIZE + ntohs(packet->LEN) * WORD_SIZE;
	if (size < MIN_PACKET_SIZE)
		size = MIN_PACKET_SIZE;
	return size;
}

#if (__GNUC__ >= 9)
#pragma GCC diagnostic pop
#endif

} // namespace sctrltp
