#pragma once
/*Definitions for return vals of SCTP funcs (will be unneccessary in Kernelspace)*/

#include <linux/types.h>
#include <stddef.h>

#include "sctrltp_defines.h"
#include "packets.h"
#include "sctp_fifo.h"
#include "sctp_atomic.h"

/*For safety we want to be sure that L1D_CLS exists and to be greater or equal to 64 Byte*/
#ifndef L1D_CLS
	#define L1D_CLS 64
#endif

#if L1D_CLS < 64
	#undef L1D_CLS
	#define L1D_CLS 64
#endif

#ifndef PTR_SIZE
	#define PTR_SIZE sizeof (void *)
#endif

#define HOSTARQ_EXIT_SIGNAL SIGUSR2
#define HOSTARQ_FAIL_SIGNAL SIGHUP

/*Helper macros*/
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

/*Configuration flags*/
#define CFG_STATUS_OK       0x01
#define CFG_STATUS_INITLO   0x02
#define CFG_STATUS_INITHI   0x03
#define CFG_STATUS_CRCERR   0x04
#define CFG_STATUS_STARTUP  0x05

/*States*/
#define STAT_NORMAL         0
#define STAT_RESET          1   /*Disables all threads to let do_reset make its work without disturbance*/
#define STAT_WAITRESET      2   /*Disables all threads excpt rx to fetch reset signal from FPGA*/

#define PARALLEL_FRAMES (L1D_CLS/PTR_SIZE)  /*Number of max frame ptrs per queue entry*/
#define LOCK_MASK_ALL   0x0001FFFF

/*Return values of internal funcs*/
#define SC_INVAL        -1
#define SC_SEGF         -2
#define SC_WOULDBLOCK   -3
#define SC_ABORT        -4
#define SC_NOMEM        -5
#define SC_FULL         -6
#define SC_EMPTY        -7
#define SC_BUSY         -8
#define SC_IOERR        -9
#define SC_CORRUPT      -10

namespace sctrltp {

struct sctp_stats {
	__u64	nr_received;    /*Number of packets received (total)*/
	__u64	nr_received_payload;    /*Number of packets with payload received*/
	__u64   nr_protofault;  /*Number of packets dropped because of false protonr and/or MAC addr*/
	__u64   nr_congdrop;    /*Number of packets dropped because of full buffers*/
	__u64	nr_outofwin;	/*Number of packets dropped because of a false seq nr*/
	__u64   nr_unknownf;    /*Number of packets dropped because of an unknown/unhandled flag*/
	__u64	bytes_sent_payload; /*Total Number of payload bytes sent*/
	__u64	bytes_sent_resend; /*Total Number of bytes resent*/
	__u64	bytes_sent;	    /*Total Number of bytes sent*/
	__u64	bytes_recv_payload;	/*Total Number of bytes received (without dropped packets)*/
	__u64	bytes_recv_oow;	/*Total Number of bytes received out of window (without dropped packets)*/
	__u64	RTT;		    /*Current estimated round trip time in milliseconds*/
};
static_assert(sizeof(struct sctp_stats) == (sizeof(__u64)*12), "");

template<typename P>
struct sctp_internal {
	struct arq_frame<P> *req;   /*pointer to packet to transmit or to packet was transmitted*/
	struct arq_frame<P> *resp;  /*pointer to packet was received (in rx_queue there is always a response and a corr. request)*/
	__u64	time;		        /*Timestamp of initial transmission*/
	__u32	ntrans;			    /*Number of transmissions (send/resend(s))*/
	__u8    acked;              /*0 = not acknowledged 1 = otherwise*/
	__u8    pad[L1D_CLS-2*PTR_SIZE-13];
};

#define PARAMETERISATION(Name, name) static_assert(sizeof(sctp_internal<Name>) == L1D_CLS, "");
#include "sctrltp/parameters.def"

template<typename P>
struct sctp_alloc {
	struct arq_frame<P> *fptr[PARALLEL_FRAMES]; /*Pointer to preallocated/recycled buffer(s)*/
	__u32 num;                                  /*Number of valid frame ptr in fptr array*/
	__u32 next;
	__u8 pad[L1D_CLS-8];                        /*We want Cachelinesize alignment*/
};

#define PARAMETERISATION(Name, name) static_assert((sizeof(sctp_alloc<Name>) % L1D_CLS) == 0, "");
#include "sctrltp/parameters.def"

template<typename P>
struct sctp_unique_queue_map {
	__u64 size;
	__u16 type[P::MAX_UNIQUE_QUEUES];
	__u8 pad[4096 - 2 * P::MAX_UNIQUE_QUEUES - 8]; /*Keep page size alignment (TODO: configurable
	                                                  page size?)*/
};

#define PARAMETERISATION(Name, name) static_assert(sizeof(sctp_unique_queue_map<Name>) == 4096, "");
#include "sctrltp/parameters.def"


template<typename P>
struct sctp_interface {                 /*Bidirectional interface between layers (lays in shared mem region)*/
	/*0-4095*/
	struct semaphore        waketx;     /*This var is used to wake TX by USER or RX*/
	__u32                   lock_mask;
	__u32                   pad0[4096/4-L1D_CLS/4-1];
	/*4096*/
	struct sctp_fifo        alloctx;
	struct sctp_fifo        allocrx;

	struct sctp_unique_queue_map<P> unique_queue_map;

	/*multiple packet type based rx queues as defined in unique_queue_map*/
	struct sctp_fifo        tx_queue;
	struct sctp_fifo        rx_queues[P::MAX_NUM_QUEUES];

	struct sctp_alloc<P>    alloctx_buf[P::ALLOCTX_BUFSIZE];
	struct sctp_alloc<P>    allocrx_buf[P::ALLOCRX_BUFSIZE];

	/*These buffers are now equally distributed among multiple fifos*/
	struct sctp_alloc<P>    txq_buf[P::TX_BUFSIZE];
	struct sctp_alloc<P>    rxq_buf[P::RX_BUFSIZE];

	struct sctp_stats       stats;
	struct arq_frame<P>     pool[P::ALLOCTX_BUFSIZE + P::ALLOCRX_BUFSIZE];

};
// TODO: check for more?

#define PARAMETERISATION(Name, name)                                                               \
	static_assert(                                                                                 \
	    offsetof(sctp_interface<Name>, alloctx) == 4096,                                           \
	    ""); // TODO: page size should be configurable
#include "sctrltp/parameters.def"

} // namespace sctrltp
