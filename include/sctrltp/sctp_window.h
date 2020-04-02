#pragma once
/*
 * Use -DWITH_CONGAV to enable congestion avoidance
 *
 * ATTENTION: This version needs locking by callers of this functions!
 * */

#include <linux/types.h>
#include <stddef.h>

#include "packets.h"
#include "sctp_atomic.h"
#include "us_sctp_defs.h"

#define SCTP_TXWIN  0
#define SCTP_RXWIN  1

namespace sctrltp {

template <typename P>
struct sctp_window {
	struct semaphore lock;		    /*A lock, for concurrent accesses (is only used by spin_lock/unlock, so theres no need to align this to pagesize)*/
	__u32	low_seq;				/*Sequencenr of Packet first transmitted, but unacknowledged*/
	__u32   pad0[L1D_CLS/4-1];
	__u32	high_seq;				/*Sequencenr of next Packet to be checked in win*/
	__u32   pad1[L1D_CLS/4-1];
	__u32   flag;                   /*Is set to 1 if retransmission ocurred and unset by congestion avoidance algortihm*/

	sctp_internal<P> *frames;	/*Pointer to buffer holding all frames (sorted by SEQ)*/
	__u32   cur_wsize;              /*Vars for slow start and congestion avoidance*/
	__u32   ss_thresh;
	__u32	max_frames;			    /*How many frames can be in frames buffer?*/
	__u32	max_wsize;				/*How large can the distance between low_seq and high_seq ever grow?*/
	__u32   side;                   /*Defines behaviour of functions (A Senderwindow is slightly different from a Receiverwin)*/
	__u32   pad2[L1D_CLS/4-6-PTR_SIZE/4-1]; /* ptr alignment requires 64 bits */

};
#define PARAMETERISATION(Name)                                                                     \
	static_assert(offsetof(sctp_window<Name>, high_seq) == (2 * L1D_CLS), "");                     \
	static_assert(offsetof(sctp_window<Name>, flag) == (3 * L1D_CLS), "");                         \
	static_assert(sizeof(sctp_window<Name>) == (4 * L1D_CLS), "");
#include "sctrltp/parameters.def"

/*Initializes sliding window 
 *Returns 0 on success otherwise a negative value*/
template <typename P>
__s8 win_init(sctp_window<P> *win, __u32 max_fr, __u32 max_ws, __u8 side);

/*Resets all necessary vars to their initial values and deletes buffercontent
 *MAKE SURE YOU HAVE ACQUIRED THE LOCK FIRST IF THIS IS CALLED!!!*/
template <typename P>
void win_reset(sctp_window<P> *win);

/*Checks if new frame can be send and then pushes frame into window
 *Returns 1 if frame was inserted otherwise 0 (<1 (errorcase!))*/
template <typename P>
__s32 new_frame_tx (sctp_window<P> *win, arq_frame<P> *new_frame, __u64 currtime);

/* Inserts frame into RXWIN if seq valid (and in window) returning slides and content of frames checked out*/
template <typename P>
__s32 new_frame_rx (sctp_window<P> *win, arq_frame<P> *in, sctp_internal<P> *out);

/*Marks the corresponding frame (equal sequencenr and nathannr) with the incoming packet
 *Returns n>0 if window was slided n times otherwise 0 (on error -1)
 *If slide was performed buffer given by out is loaded with data to pass to user*/
template <typename P>
__s32 mark_frame (sctp_window<P> *win, __u32 rACK, sctp_internal<P> *out);

/*Compares time field of packet(s) with currtime and gives them back if difference exceeds rto*/
template <typename P>
__s32 resend_frame (sctp_window<P> *win, sctp_internal<P> *resend, __u64 rto, __u64 currtime);

} // namespace sctrltp
