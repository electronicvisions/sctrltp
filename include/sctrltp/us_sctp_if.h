#pragma once
/*Interface definition for upper layers*/


#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "sctp_atomic.h"
#include "sctp_fifo.h"
#include "packets.h"
#include "us_sctp_defs.h"

/*mode used to provide mutual exclusion between concurrent calls to API*/
#define MODE_SAFE       0x01
/*mode used to lock and access queues without waiting
 * locking failed: SC_BUSY is returned
 * queue empty/full: SC_EMPTY/FULL is returned*/
#define MODE_NONBLOCK   0x02
/*mode used to force push to queue*/
#define MODE_FLUSH      0x04
/*this mode is for rel_buf to put a buffer back in local sending cache*/
#define MODE_TX         0x08

struct sctp_rx_cache {
	struct sctp_alloc in[NUM_QUEUES];
	struct sctp_alloc out;
};

struct sctp_tx_cache {
	struct sctp_alloc in;
	struct sctp_alloc out[NUM_QUEUES];
};

struct sctp_descr {
	struct drepper_mutex    mutex;      /*a mutex similar to pthread mutexes*/

	struct sctp_tx_cache    send_buf;
	struct sctp_rx_cache    recv_buf;

	struct sctp_interface   *trans;	    /*Pointer to interface in shared memory to pass and receive data to/from*/
	__u8                    name[248];  /*Name of shared mem segment*/
	__u32                   my_lock_mask; /*Mask which determines nathans currently locked by me*/
	__s32					ref_cnt;    /*reference counter*/

	__u8                    pad[8192 - (248 + PTR_SIZE + sizeof(struct drepper_mutex) + 2*sizeof(struct sctp_tx_cache) + 8)];
};
static_assert(sizeof(struct sctp_descr) == (2*4096), ""); // 2 pages (TODO: page size should be configurable)

struct buf_desc {
	/* TODO: do we need raw UDP frame? */
	struct arq_frame *arq_sctrl;
	__u64 *payload;
};

/*abstract functions to use framework more efficiently*/
struct sctp_descr *open_conn (const char *corename);

__s32 close_conn (struct sctp_descr *desc);

__s32 acq_buf (struct sctp_descr *desc, struct buf_desc *acq, const __u8 mode);

__s32 rel_buf (struct sctp_descr *desc, struct buf_desc *rel, const __u8 mode);

__s32 send_buf (struct sctp_descr *desc, struct buf_desc *buf, const __u8 mode);

__s32 recv_buf (struct sctp_descr *desc, struct buf_desc *buf, const __u8 mode);

__s32 init_buf (struct buf_desc *buf);

__s32 append_words (struct buf_desc *buf, const __u16 ptype, const __u32 num, const __u64 *values);

__s32 tx_queues_empty (struct sctp_descr *desc);

__s32 tx_queues_full (struct sctp_descr *desc);

__s32 rx_queues_empty (struct sctp_descr *desc);

__s32 rx_queues_full (struct sctp_descr *desc);

/*This function connects to SCTP Core and returning a descriptor (on error returning NULL)*/
struct sctp_descr *SCTP_Open (const char *corename);

/*Disconnects from SCTP Core and gives statuscode back*/
__s32 SCTP_Close (struct sctp_descr *desc);

/*Assembles an SCTP Packet respective to aflags and sflags and filling it with payload
 *returns num on success otherwise negative value*/
__s64 SCTP_Send (struct sctp_descr *desc, const __u16 typ, const __u32 num, const __u64 *payload);

/*Fetches one packet from core and gives the amount of responses(!) and flags in it back
 *returns 0 on success otherwise negative value*/
__s32 SCTP_Recv (struct sctp_descr *desc, __u16 *typ, __u16 *num, __u64 *resp);
