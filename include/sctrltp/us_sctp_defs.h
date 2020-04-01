#pragma once
/*Definitions for return vals of SCTP funcs (will be unneccessary in Kernelspace)*/

#include <linux/types.h>
#include <stddef.h>

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

/*Helper macros*/
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

/*Configuration flags*/
#define CFG_STATUS_OK       0x01
#define CFG_STATUS_INITLO   0x02
#define CFG_STATUS_INITHI   0x03
#define CFG_STATUS_CRCERR   0x04
#define CFG_STATUS_STARTUP  0x05

/* DELAY = MAX_WINSIZ * PDU_SIZE / WIRESPEED */
#define MIN_RTO    ((MAX_WINSIZ * PDU_SIZE) / WIRESPEED) / 2
#define MAX_RTO    MIN_RTO
//#define DELAY_ACK  ((MAX_WINSIZ * PDU_SIZE) / WIRESPEED / 3) /*maximum number of microseconds an ACK signal will be delayed*/
#define DELAY_ACK  500

/*Core constraints*/
#define TO_RES            100    /*Timeout resolution in microseconds*/
#define MAX_TRANS       10000   /*maximum number of transmission till warning!!!*/
#define RESET_TIMEOUT 2000*1000 /*in us*/

#if DELAY_ACK < TO_RES
#error DELAY_ACK is smaller than TO_RES
#endif

#define STAT_NORMAL         0
#define STAT_RESET          1   /*Disables all threads to let do_reset make its work without disturbance*/
#define STAT_WAITRESET      2   /*Disables all threads excpt rx to fetch reset signal from FPGA*/

#define PARALLEL_FRAMES (L1D_CLS/PTR_SIZE)  /*Number of max frame ptrs per queue entry*/
#define NUM_QUEUES               1
#define LOCK_MASK_ALL   0x0001FFFF

/*Internal buffer sizes*/
#define ALLOCTX_BUFSIZE   (MAX_WINSIZ * NUM_QUEUES * 4)
#define ALLOCRX_BUFSIZE   (ALLOCTX_BUFSIZE*2)
#define TX_BUFSIZE        (ALLOCTX_BUFSIZE)
#define RX_BUFSIZE        (ALLOCRX_BUFSIZE)

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

struct sctp_internal {
	struct arq_frame *req;   /*pointer to packet to transmit or to packet was transmitted*/
	struct arq_frame *resp;  /*pointer to packet was received (in rx_queue there is always a response and a corr. request)*/
	__u64	time;		        /*Timestamp of initial transmission*/
	__u32	ntrans;			    /*Number of transmissions (send/resend(s))*/
	__u8    acked;              /*0 = not acknowledged 1 = otherwise*/
	__u8    pad[L1D_CLS-2*PTR_SIZE-13];
};
static_assert(sizeof(struct sctp_internal) == L1D_CLS, "");

struct sctp_alloc {
	struct arq_frame  *fptr[PARALLEL_FRAMES]; /*Pointer to preallocated/recycled buffer(s)*/
	__u32 num;                                  /*Number of valid frame ptr in fptr array*/
	__u32 next;
	__u8 pad[L1D_CLS-8];                        /*We want Cachelinesize alignment*/
};
static_assert((sizeof(struct sctp_alloc) % L1D_CLS) == 0, "");

struct sctp_interface {                 /*Bidirectional interface between layers (lays in shared mem region)*/
	/*0-4095*/
	struct semaphore        waketx;     /*This var is used to wake TX by USER or RX*/
	__u32                   lock_mask;
	__u32                   pad0[4096/4-L1D_CLS/4-1];
	/*4096*/
	struct sctp_fifo        alloctx;
	struct sctp_fifo        allocrx;

	/*multiple tx and rx queues to enable nathan based routing*/
	struct sctp_fifo        tx_queues[NUM_QUEUES];
	struct sctp_fifo        rx_queues[NUM_QUEUES];

	struct sctp_alloc       alloctx_buf[ALLOCTX_BUFSIZE];
	struct sctp_alloc       allocrx_buf[ALLOCRX_BUFSIZE];

	/*These buffers are now equally distributed among multiple fifos*/
	struct sctp_alloc       txq_buf[TX_BUFSIZE];
	struct sctp_alloc       rxq_buf[RX_BUFSIZE];

	struct sctp_stats       stats;
	struct arq_frame        pool[ALLOCTX_BUFSIZE+ALLOCRX_BUFSIZE];
};
static_assert(offsetof(struct sctp_interface, alloctx) == 4096, ""); // TODO: page size should be configurable
// TODO: check for more?

} // namespace sctrltp
