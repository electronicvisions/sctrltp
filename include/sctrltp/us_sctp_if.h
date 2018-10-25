#pragma once
/*Interface definition for upper layers*/
#pragma once

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

namespace sctrltp {

template<typename P>
struct sctp_rx_cache {
	struct sctp_alloc<P> in[P::MAX_NUM_QUEUES];
	struct sctp_alloc<P> out;
};

template<typename P>
struct sctp_tx_cache {
	struct sctp_alloc<P> in;
	struct sctp_alloc<P> out;
};

template<typename P>
struct sctp_descr {
	struct drepper_mutex    mutex;      /*a mutex similar to pthread mutexes*/

	sctp_tx_cache<P>        send_buf;
	sctp_rx_cache<P>        recv_buf;

	sctp_interface<P>       *trans;	    /*Pointer to interface in shared memory to pass and receive data to/from*/
	__u8                    name[248];  /*Name of shared mem segment*/
	__u32                   my_lock_mask; /*Mask which determines nathans currently locked by me*/
	__s32					ref_cnt;    /*reference counter*/

	__u8                    pad[8192 - (248 + PTR_SIZE + sizeof(drepper_mutex) + sizeof(sctp_tx_cache<P>) + sizeof(sctp_rx_cache<P>) + 8)];
};
#define PARAMETERISATION(Name, name)                                                               \
	static_assert(                                                                                 \
	    sizeof(sctp_descr<Name>) == (2 * 4096),                                                    \
	    ""); // 2 pages (TODO: page size should be configurable)
#include "sctrltp/parameters.def"

template<typename P>
struct buf_desc {
	/* TODO: do we need raw UDP frame? */
	arq_frame<P> *arq_sctrl;
	__u64 *payload;
};

/*abstract functions to use framework more efficiently*/
template<typename P>
sctp_descr<P> *open_conn (const char *corename);

template<typename P>
__s32 close_conn (sctp_descr<P> *desc);

template<typename P>
__s32 acq_buf (sctp_descr<P> *desc, buf_desc<P> *acq, const __u8 mode);

template<typename P>
__s32 rel_buf (sctp_descr<P> *desc, buf_desc<P> *rel, const __u8 mode);

template<typename P>
__s32 send_buf (sctp_descr<P> *desc, buf_desc<P> *buf, const __u8 mode);

template<typename P>
__s32 recv_buf (sctp_descr<P> *desc, buf_desc<P> *buf, const __u8 mode);

template<typename P>
__s32 recv_buf (struct sctp_descr<P> *desc, struct buf_desc<P> *buf, const __u8 mode, __u64 idx);

template<typename P>
__s32 get_next_frame_pid (struct sctp_descr<P> *desc);

template<typename P>
__s32 init_buf (struct buf_desc<P> *buf);

template<typename P>
__s32 append_words (buf_desc<P> *buf, const __u16 ptype, const __u32 num, const __u64 *values);

template<typename P>
__s32 tx_queue_empty (struct sctp_descr<P> *desc);

template<typename P>
__s32 tx_queue_full (struct sctp_descr<P> *desc);

template<typename P>
__s32 rx_queue_empty (struct sctp_descr<P> *desc);

template<typename P>
__s32 rx_queue_empty (struct sctp_descr<P> *desc, __u64 idx);

template<typename P>
__s32 rx_queue_full (struct sctp_descr<P> *desc);

template<typename P>
__s32 rx_queue_full (struct sctp_descr<P> *desc, __u64 idx);

/*This function connects to SCTP Core and returning a descriptor (on error returning NULL)*/
template<typename P>
sctp_descr<P> *SCTP_Open (const char *corename);

/*Disconnects from SCTP Core and gives statuscode back*/
template<typename P>
__s32 SCTP_Close (sctp_descr<P> *desc);

/*Assembles an SCTP Packet respective to aflags and sflags and filling it with payload
 *returns num on success otherwise negative value*/
template<typename P>
__s64 SCTP_Send (sctp_descr<P> *desc, const __u16 typ, const __u32 num, const __u64 *payload);

/*Fetches one packet from core and gives the amount of responses(!) and flags in it back
 *returns 0 on success otherwise negative value*/
template<typename P>
__s32 SCTP_Recv (sctp_descr<P> *desc, __u16 *typ, __u16 *num, __u64 *resp);

} // namespace sctrltp
