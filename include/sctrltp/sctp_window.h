/*
 * Use -DWITH_CONGAV to enable congestion avoidance
 *
 * ATTENTION: This version needs locking by callers of this functions!
 * */

#ifndef _SCTP_WINDOW
#define _SCTP_WINDOW

#include <linux/types.h>
#include "packets.h"
#include "sctp_atomic.h"
#include "us_sctp_defs.h"

#define SCTP_TXWIN  0
#define SCTP_RXWIN  1

struct sctp_window {
	struct semaphore lock;		    /*A lock, for concurrent accesses (is only used by spin_lock/unlock, so theres no need to align this to pagesize)*/
	__u32	low_seq;				/*Sequencenr of Packet first transmitted, but unacknowledged*/
	__u8    pad0[L1D_CLS-4];
	__u32	high_seq;				/*Sequencenr of next Packet to be checked in win*/
	__u8    pad1[L1D_CLS-4];
	__u8    flag;                   /*Is set to 1 if retransmission ocurred and unset by congestion avoidance algortihm*/

	struct sctp_internal *frames;	/*Pointer to buffer holding all frames (sorted by SEQ)*/
	__u32   cur_wsize;              /*Vars for slow start and congestion avoidance*/
	__u32   ss_thresh;
	__u32	max_frames;			    /*How many frames can be in frames buffer?*/
	__u32	max_wsize;				/*How large can the distance between low_seq and high_seq ever grow?*/
	__u8    side;                   /*Defines behaviour of functions (A Senderwindow is slightly different from a Receiverwin)*/
	__u8    pad2[L1D_CLS-18-PTR_SIZE];
} __attribute__ ((packed));

/*Initializes sliding window 
 *Returns 0 on success otherwise a negative value*/
__s8 win_init (struct sctp_window *win, __u32 max_fr, __u32 max_ws, __u8 side);

/*Resets all necessary vars to their initial values and deletes buffercontent
 *MAKE SURE YOU HAVE ACQUIRED THE LOCK FIRST IF THIS IS CALLED!!!*/
void win_reset (struct sctp_window *win);

/*Checks if new frame can be send and then pushes frame into window
 *Returns 1 if frame was inserted otherwise 0 (<1 (errorcase!))*/
__s32 new_frame_tx (struct sctp_window *win, struct arq_frame *new, __u64 currtime);

/* Inserts frame into RXWIN if seq valid (and in window) returning slides and content of frames checked out*/
__s32 new_frame_rx (struct sctp_window *win, struct arq_frame *in, struct sctp_internal *out);

/*Marks the corresponding frame (equal sequencenr and nathannr) with the incoming packet
 *Returns n>0 if window was slided n times otherwise 0 (on error -1)
 *If slide was performed buffer given by out is loaded with data to pass to user*/
__s32 mark_frame (struct sctp_window *win, __u32 rACK, struct sctp_internal *out);

/*Compares time field of packet(s) with currtime and gives them back if difference exceeds rto*/
__s32 resend_frame (struct sctp_window *win, struct sctp_internal *resend, __u64 rto, __u64 currtime);

#endif
