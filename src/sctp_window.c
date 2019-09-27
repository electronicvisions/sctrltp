/*ATTENTION: Some funcs do not acquire locks when reading but they are static :)
 *TODO: I. use kmalloc in kernelspace*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "sctrltp/sctp_window.h"
#include "sctrltp/us_sctp_sock.h" /* debug helper functions */


static inline struct sctp_internal *get_frame (struct sctp_window *win, __u32 seq)
{
	struct sctp_internal *tmp;
	tmp = win->frames;
	tmp += seq;
	return tmp;
}

static inline __u8 is_marked_frame (struct sctp_window *win, __u32 seq)
{
	struct sctp_internal *tmp;
	tmp = get_frame (win, seq);
	if (tmp->acked) return 1;
	return 0;
}

static inline __u8 is_in_window (struct sctp_window *win, __u32 seq)
{
	__u32 winsize;
	__u32 distsl;
	__u32 disths;
	__u32 max;
	max = win->max_frames;
	winsize = (win->high_seq - win->low_seq)%max;
	disths = (win->high_seq - seq)%max;
	distsl = (seq - win->low_seq)%max;
	return ((disths <= winsize)&&(distsl < winsize));
}

static __u8 is_win_full (struct sctp_window *win) {
#ifndef WITH_CONGAV
	return (((win->high_seq - win->low_seq)%win->max_frames) == win->max_wsize);
#else
	return (((win->high_seq - win->low_seq)%win->max_frames) == (win->cur_wsize / sizeof (struct arq_frame)));
#endif
}

/*TODO: Rename malloc to kmalloc when in kernel space*/
__s8 win_init (struct sctp_window *win, __u32 max_fr, __u32 max_ws, __u8 side)
{
	__u8 *tmp;
	if (win)
	{
		/*Create buffer and store it*/
		tmp = malloc (sizeof(struct sctp_internal)*max_fr);
		if (!tmp) return SC_NOMEM;

		win->side = side;
		win->max_frames = max_fr;
		win->max_wsize = max_ws;
		win->low_seq = 0;

		if (side == SCTP_RXWIN) {
			win->high_seq = max_ws;
		} else {
			win->high_seq = 0;
#ifdef WITH_CONGAV
			/*set wsize = 1*/
			win->flag = 0;
			win->cur_wsize = sizeof (struct arq_frame);
			win->ss_thresh = max_ws * sizeof(struct arq_frame) / 2;
#endif
		}

		win->frames = (struct sctp_internal *)tmp;
		memset (tmp, 0, sizeof(struct sctp_internal)*max_fr);
		atomic_write (&(win->lock.lock), 0);
		return 1;
	}
	return SC_INVAL;
}


/*ATTENTION: Acquire lock to window first!!*/
void win_reset (struct sctp_window *win)
{
	if (win)
	{
		win->low_seq = 0;

		if (win->side == SCTP_RXWIN) {
			win->high_seq = win->max_wsize;
		} else {
			win->high_seq = 0;
#ifdef WITH_CONGAV
			/*set wsize = 1*/
			win->flag = 0;
			win->cur_wsize = sizeof (struct arq_frame);
			win->ss_thresh = win->max_wsize * sizeof(struct arq_frame) / 2;
#endif
		}

		/*Delete buffer content if there is such a buffer*/
		if (win->frames) memset (win->frames, 0, sizeof(struct sctp_internal)*win->max_frames);
	}
}

__s32 new_frame_tx (struct sctp_window *win, struct arq_frame *new, __u64 currtime)
{
	__u32 seq;
	/*__u32 max_frames;*/
	__s32 ret = SC_ABORT;
	struct sctp_internal *tmp;

#ifdef WITH_CONGAV
	if (is_win_full(win) || win->flag) {
		/*Window is full or congestion ocurred, lets get out*/
		return 0;
	}
#else
	if (is_win_full(win)) {
		/*Window is full, lets get out*/
		return 0;
	}
#endif

	seq = win->high_seq;

	tmp = get_frame(win, seq);

	if ((!tmp->req)) {
		/*Success! We can append a new frame in buffer*/
		/*Give packet the new sequence number!*/
		sctpreq_set_seq(new, seq);

		tmp->time = currtime;  /*This is initialized to current timestamp*/
		tmp->ntrans = 1;  /*After this call we will send the frame one time minimum*/
		tmp->acked = 0;
		tmp->req = new; /*Register pointer of frame in buffer*/


		/*Increase high_seq locally*/
		seq++;
		seq = seq % win->max_frames;
		win->high_seq = seq;
		ret = 1;
	}

	return ret;
}

__s32 new_frame_rx (struct sctp_window *win, struct arq_frame *in, struct sctp_internal *out)
{
	__u32 seq;
	__u32 high_seq;
	__u32 max_frames;
	__u32 slides = 0;
	__s32 ret = 0;
	struct sctp_internal *tmp;

	max_frames = win->max_frames;

	/*If seq was invalid we have nothing to do!!*/
	/*Seq has to be checked before calling mark_frame!!!*/
	seq = (__u32)sctpreq_get_seq(in);

	tmp = get_frame (win, seq);

	/*Check if seq is in window boundary and entry isnt marked already*/
	if ((!tmp->acked) && (is_in_window(win,seq))) {

		/*Sequencenr is in window boundary*/
		tmp->resp = in;
		tmp->acked = 1;

		/* Check if we could slide our window */
		if (seq == win->low_seq) {

			high_seq = win->high_seq;
			/*In fact, we can slide (slide till windowsize is zero,or an unmarked frame reached)*/
			while ((seq != high_seq)&&(tmp->acked)) {
				/*Copy frame into outbuffer*/
				memcpy (out, tmp, sizeof(struct sctp_internal));
				tmp->resp = NULL;
				tmp->acked = 0;

				/*Increase counter/pointer*/
				out++;
				slides++;
				seq++;
				seq %= max_frames;
				tmp = get_frame (win, seq);
			}

			win->low_seq = seq;
			win->high_seq = (seq+win->max_wsize)%max_frames;

			ret = slides;
		}
	} else {
		ret = SC_INVAL;	/*Drop frame, its out of win!!!*/
	}

	return ret;
}

__s32 mark_frame (struct sctp_window *win, __u32 rACK, struct sctp_internal *out)
{
	__u32 seq;
	__u32 high_seq;
	__u32 max_frames;
	__s32 ret = 0;
	struct sctp_internal *tmp;
#ifdef WITH_CONGAV
	__u32 cur_wsize;
	__u32 max_wsize;
#endif

	max_frames = win->max_frames;

	high_seq = rACK;

	/*Check if seq is in window boundary*/
	if (is_in_window(win,high_seq)) {

		/*Sequencenr is in window boundary*/
		high_seq++;
		high_seq %= max_frames;
		seq = win->low_seq;

		/*In fact, we can slide till we reached the first unacknowledged frame*/
		while (seq != high_seq) {
			/*Copy frame into outbuffer*/
			tmp = get_frame (win, seq);
			memcpy (out, tmp, sizeof(struct sctp_internal));
			tmp->req = NULL;
			tmp->acked = 1;

			/*Increase counter/pointer*/
			out++;
			ret++;
			seq++;
			seq %= max_frames;
		}

		win->low_seq = seq;

#ifdef WITH_CONGAV
		if (win->flag) {
			/*on congestion: let win become empty and set wsize = 1 (in bytes not packets) (initiate slow start)*/
			if (win->low_seq == win->high_seq) {
				//printf("Congestion!\n");
				/*Window is now empty*/
				win->ss_thresh = win->cur_wsize / 2;
				win->cur_wsize = sizeof (struct arq_frame);
				win->flag = 0;
			}
		} else {
			/*on no congestion do slow start or congestion avoidance to probe for more bandwith*/
			cur_wsize = win->cur_wsize;
			max_wsize = win->max_wsize;
			if (cur_wsize < win->ss_thresh) {
				/*slow start*/
				cur_wsize += sizeof (struct arq_frame);
			} else {
				/*congestion avoidance strategy*/
				/* cur_wsize += (1-(curr_wsize/max_wsize))*packetsize */
				cur_wsize += ((100 - (cur_wsize*100)/(max_wsize * sizeof(struct arq_frame))) * sizeof(struct arq_frame))/100;
			}
			if (cur_wsize > (max_wsize * sizeof (struct arq_frame))) cur_wsize = max_wsize * sizeof (struct arq_frame);
			win->cur_wsize = cur_wsize;
		}
#endif
	} else {
		ret = SC_INVAL;	/*Drop frame, its out of win!!!*/
	}

	return ret;
}

/*ATTENTION: Lock window before calling resend_frame!!!*/
__s32 resend_frame (struct sctp_window *win, struct sctp_internal *resend, __u64 rto, __u64 currtime)
{
	__u32 ret = 0;
	__u32 seq;
	__u32 high;
	__u32 max;

	struct sctp_internal *tmp;

	seq = win->low_seq;
	high = win->high_seq;
	max = win->max_frames;
	/*Cycle through window to find frames to be resent*/
	while (seq != high) {
		tmp = &(win->frames[seq]);
		/*Is this frame not checked out yet?*/
		if (tmp->req) {
			/*Check if timeout for this packet has run out*/
			if (((currtime - tmp->time) / rto) >= (tmp->ntrans)) {
				tmp->ntrans++;
				if (tmp->ntrans >= MAX_TRANS) {
					fprintf (stderr, "MASTER TIMEOUT: maximum number of transmissions reached (%d)!!\n", tmp->ntrans);
					print_stats();
					return -1;
				}

				/*Set flag to notify mark_frame of retransmit*/
				win->flag = 1;
				memcpy (&(resend[ret]), tmp, sizeof(struct sctp_internal));
				/*Increase number of frames to be resent*/
				ret++;
				//ret = 1;
			}
		}
		seq = (seq+1) % max;
	}

	return ret;
}
