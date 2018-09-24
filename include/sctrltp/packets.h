#pragma once

/** \brief Headerfile for Packethandling in SCTP
 *	Implements some short but necessary funcs to de/encapsulate packetcontents
*/


#include <linux/types.h>
#include <arpa/inet.h>

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

extern uint64_t const resetframe_var_values_check[6];

struct arq_frame {
	__u32   ACK;                /*Acknowledge to packet with sequenceno = ACK (other direction)*/
	__u32   SEQ;                /*Sequencenumber*/
	__u16   PTYPE;              /*Type of packet (called "packet id" in docs)*/
	__u16   LEN;                /*Length (64-bit words)*/
	__u64   COMMANDS[MAX_PDUWORDS];
}__attribute__ ((packed));


struct arq_ackframe {
	__u32   ACK;                /*Acknowledge to packet with sequenceno = ACK (other direction)*/
}__attribute__ ((packed));

struct arq_resetframe {
	uint32_t magic_word;
}__attribute__ ((packed));


/**** FUNCS USED BY SCTP LAYER ****/

void parse_mac (char *in, __u8 *out);
void print_mac (const char *prefix, __u8 *mac);


/*SCTPREQ_* These funtions are used by the sending side of the layer*/

struct arq_frame *sctpreq_get_ptr (struct arq_frame *packet);


// TODO: static inline, but C99 style extern inline seems nicer...

static inline void sctpreq_set_header (struct arq_frame *packet, __u16 LEN, __u16 PTYPE) {
	packet->LEN = htons(LEN);
	packet->PTYPE = htons(PTYPE);
} __attribute__((always_inline))

/*seq valid bit will be set too!!*/
__attribute__((always_inline)) static inline void sctpreq_set_seq (struct arq_frame *packet, __u32 SEQ) {
	packet->SEQ = htonl(SEQ); /* size: 7 bits! */
}

__attribute__((always_inline)) static inline void sctpreq_set_ack (struct arq_frame *packet, __u32 ACK) {
	packet->ACK = htonl(ACK);
}

__attribute__((always_inline)) static inline void sctpack_set_ack (struct arq_ackframe *packet, __u32 ACK) {
	packet->ACK = htonl(ACK);
}

/*get length in words*/
__attribute__((always_inline)) static inline __u16 sctpreq_get_len (struct arq_frame *packet) {
	return ntohs(packet->LEN);
}

/*get size in bytes*/
__attribute__((always_inline)) static inline __u32 sctpreq_get_size (struct arq_frame *packet) {
	__u32 size = ARQ_HEADER_SIZE;
	size += TYPLEN_SIZE + ntohs(packet->LEN) * WORD_SIZE;
	if (size < MIN_PACKET_SIZE)
		size = MIN_PACKET_SIZE;
	return size;
}

__attribute__((always_inline)) static inline __u32 sctpreq_get_seq (struct arq_frame *packet) {
	__u32 seq;
	seq = ntohl(packet->SEQ);
	return seq;
}

__attribute__((always_inline)) static inline __u32 sctpreq_get_ack (struct arq_frame *packet) {
	return ntohl(packet->ACK);
}

__attribute__((always_inline)) static inline __u16 sctpreq_get_typ (struct arq_frame *packet) {
	return ntohs(packet->PTYPE);
}

__attribute__((always_inline)) static inline __u64* sctpreq_get_pload (struct arq_frame *packet) {
	return packet->COMMANDS;
}

static inline void sctpreset_init (struct arq_resetframe *packet) {
	packet->magic_word = htonl(HW_HOSTARQ_MAGICWORD);
}


__attribute__((always_inline)) static inline __u32 sctpsomething_get_size (struct arq_frame *packet, size_t nread) {
	__u32 size = ARQ_HEADER_SIZE;
	if (nread <= MIN_PACKET_SIZE)
		return MIN_PACKET_SIZE;
	size += TYPLEN_SIZE + ntohs(packet->LEN) * WORD_SIZE;
	if (size < MIN_PACKET_SIZE)
		size = MIN_PACKET_SIZE;
	return size;
}
