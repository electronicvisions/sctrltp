#pragma once

#include <linux/types.h>
#include <unordered_set>

#define MTU               1500
#define MIN_PACKET_SIZE      4
#define MIN_PACKET_SEND_SIZE (1*8+12)

#define ETH_HEADER_SIZE     14 /* in bytes */
#define ARQ_HEADER_SIZE      8 /* in bytes */
#define TYPLEN_SIZE          4 /* in bytes */

#define PDU_SIZE          (MTU - ETH_HEADER_SIZE - ARQ_HEADER_SIZE)
#define WORD_SIZE            8 /* 64-bit words! */


#define PTYPE_FLUSH        0x8000 /* flush data */
#define PTYPE_LOOPBACK     0x8001 /* loopback data */
#define PTYPE_CFG_TYPE     0x8002 /* configure fpga */
#define PTYPE_SENDDUMMY    0x8003 /* set fpga to send dummy data */
#define PTYPE_STATS        0x8004 /* fpga stats module */
#define PTYPE_PERFTEST     0x8006 /* set fpga to send data */
#define PTYPE_DUMMYDATA0   0x0000
#define PTYPE_DUMMYDATA1   0x0001
#define PTYPE_ARQSTAT      0x0002
#define PTYPE_DO_ARQRESET  0x5000
/* FIXME: more PTYPES here... enum unroll maybe? */


#define CFG_SIZE                      3

#define HW_HOSTARQ_MAGICWORD 0xABABABAB

namespace sctrltp {

typedef __u16 packetid_t;
typedef std::unordered_set<packetid_t> unique_queue_set_t;

template<
	size_t I_MAX_WINSIZ = 128,   /* Can be specified in the range of 1 till (MAX_NRFRAMES-1)/2 (compile-time check exists) */
	size_t I_MAX_NRFRAMES = 256, /* Maximum number of frames in buffer equals maximum SEQ number + 1 */
	size_t I_WIRESPEED = 125,    /* in 10^6 bytes/sec (because we calculate in us) */
	size_t I_MAX_PDUWORDS = 180, /* could be (PDU_SIZE/8)? */
	size_t I_HW_MASTER_TIMEOUT = 0x000fffff, /* 3000*125 , 3000us is too large! */
	size_t I_HW_DELAY_ACK = 1023,            /* maximum (was 100*125 = 100us */
	size_t I_HW_FLUSH_COUNT = 0x0000ffff,    /*  all bits on :) */
	size_t I_ALLOC_BUF_FACTOR = 4 /* heuristic factor for tx and rc buffer size  */
>
struct Parameters
{
	constexpr static size_t MAX_WINSIZ = I_MAX_WINSIZ;
	constexpr static size_t MAX_NRFRAMES = I_MAX_NRFRAMES;
	static_assert((MAX_WINSIZ * 2) <= MAX_NRFRAMES, "reduce window size to max. seq number / 2 => ((MAX_NRFRAMES-1)/2)!");
	constexpr static size_t WIRESPEED = I_WIRESPEED;
	constexpr static size_t MAX_PDUWORDS = I_MAX_PDUWORDS;
	constexpr static size_t HW_MASTER_TIMEOUT = I_HW_MASTER_TIMEOUT;
	constexpr static size_t HW_DELAY_ACK = I_HW_DELAY_ACK;
	constexpr static size_t HW_FLUSH_COUNT = I_HW_FLUSH_COUNT;

	constexpr static size_t MAX_UNIQUE_QUEUES = 10;
	/* queue 0 is reserved for all non-unique packet types */
	constexpr static size_t MAX_NUM_QUEUES = MAX_UNIQUE_QUEUES + 1;

	constexpr static size_t ALLOCTX_BUFSIZE = I_MAX_WINSIZ * I_ALLOC_BUF_FACTOR;
	constexpr static size_t ALLOCRX_BUFSIZE = ALLOCTX_BUFSIZE * MAX_NUM_QUEUES * 2;
	constexpr static size_t TX_BUFSIZE = ALLOCTX_BUFSIZE;
	constexpr static size_t RX_BUFSIZE = ALLOCRX_BUFSIZE;

	constexpr static size_t MIN_RTO = ((I_MAX_WINSIZ * PDU_SIZE) / I_WIRESPEED) / 2; /* us */
	constexpr static size_t MAX_RTO = MIN_RTO;
	constexpr static size_t DELAY_ACK = 500;

	constexpr static size_t TO_RES = 100; /*Timeout resolution in microseconds*/
	constexpr static size_t MAX_TRANS = 10000; /*maximum number of transmission till warning!!!*/
	static_assert(DELAY_ACK > TO_RES);
};

typedef Parameters<> ParametersFcpBss1;
typedef Parameters<16384, 65536, 125, 180, 0x000fffff, 1023, 0x0000ffff, 1> ParametersFcpBss2Cube;
typedef Parameters<32, 512, 125, 126, 0x000ffff, 1023, 0x0000ffff, 4> ParametersAnanasBss1;

} // namespace sctrltp
