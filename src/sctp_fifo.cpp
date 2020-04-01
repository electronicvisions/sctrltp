/*NOTE: Normal fif_* funcs are for single writer/reader only; use m*_fif_* funcs if there are more*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "sctrltp/sctp_atomic.h"
#include "sctrltp/sctp_fifo.h"

 void *get_abs_ptr (void *baseptr, void *relptr)
{
	void *tmp;
#ifdef __i386__
	tmp = (void *)(((__u32)baseptr)+((__u32)relptr));
#else
	tmp = (void *)(((__u64)baseptr)+((__u64)relptr));
#endif
	return tmp;
}

 void *get_rel_ptr (void *baseptr, void *absptr)
{
	void *tmp;
#ifdef __i386__
	tmp = (void *)(((__u32)absptr)-((__u32)baseptr));
#else
	tmp = (void *)(((__u64)absptr)-((__u64)baseptr));
#endif
	return tmp;
}

/*TODO: Exchange malloc with kmalloc when in kernelspace*/
__s8 fif_init (struct sctp_fifo *fifo, __u32 nr, __u32 size)
{
	__u8 *tmp;
	if (fifo) {
		/*Create buffer*/
		tmp = static_cast<__u8*>(malloc(nr*size));
		if (!tmp) {
			return -5;
		}
		memset (tmp, 0, nr*size);
		fifo->last_out = 0;
		fifo->last_in = 0;
		fifo->nr_elem = nr;
		fifo->elem_size = size;
		fifo->buf = tmp;
		/*Make fifo accessible*/
		cond_init (&(fifo->signals));
		semaph_init (&(fifo->nr_full), 0);
		return 0;
	}
	return -1;
}

__s8 fif_init_wbuf (struct sctp_fifo *fifo, __u32 nr, __u32 size, __u8 *buf, void *baseptr)
{
	if (fifo) {
		if (fifo->buf) return -1;
		/*Create buffer*/
		memset (buf, 0, nr*size);
		fifo->last_out = 0;
		fifo->last_in = 0;
		fifo->nr_elem = nr;
		fifo->elem_size = size;

		/*Calculates relative pointer to a given pointer*/
		if (baseptr) buf = static_cast<__u8*>(get_rel_ptr(baseptr, buf));

		fifo->buf = buf;
		/*Make fifo accessible*/
		cond_init (&(fifo->signals));
		semaph_init (&(fifo->nr_full), 0);
		return 0;
	}
	return -1;
}

/*ATTENTION: acquire ALL locks before resetting!!!*/
void fif_reset (struct sctp_fifo *fifo)
{
	if (fifo) {
		/*Now noone can access fifo struct now*/
		fifo->last_out = 0;
		fifo->last_in = 0;

		fifo->signals.semval = 0;
		fifo->nr_full.semval = 0;
	}
}

__s8 fif_push (struct sctp_fifo *fifo, __u8 *elem, void *baseptr)
{
	__u32 offset;
	__u8 *absptr;

	__s32 c;
	if (fifo) {
		/*Check, if there are empty elements available*/
		spin_lock (&(fifo->nr_full.lock));
		while ((c = fifo->nr_full.semval) >= (__s32)fifo->nr_elem) {
			spin_unlock (&(fifo->nr_full.lock));

			assert(cond_wait(&(fifo->signals), FIF_SIG_DATAOUT) == FIF_SIG_DATAOUT);

			spin_lock (&(fifo->nr_full.lock));
		}

		/*Increase pointer to last element registered*/
		fifo->last_in = (fifo->last_in + 1) % fifo->nr_elem;
		offset = fifo->last_in;

		if (baseptr) {
			absptr = (__u8 *)get_abs_ptr (baseptr, fifo->buf);
		} else absptr = fifo->buf;

		/*We are safe to put element into buffer*/
		memcpy (absptr+(offset * fifo->elem_size), elem, fifo->elem_size);

		/*Signal, that there is a new full element*/
		fifo->nr_full.semval++;
		spin_unlock (&(fifo->nr_full.lock));

		/*Wake at least one consumer*/
		cond_signal (&(fifo->signals), FIF_SIG_DATAIN, INT_MAX);
		return 0;
	}
	return -1;
}

__s8 fif_pop (struct sctp_fifo *fifo, __u8 *elem, void *baseptr)
{
	__u32 offset;
	__u8 *absptr;

	__s32 c;
	if (fifo) {
		/*Check, if there are full elements*/
		spin_lock (&(fifo->nr_full.lock));
		while ((c = fifo->nr_full.semval) <= 0) {
			spin_unlock (&(fifo->nr_full.lock));

			assert(cond_wait(&(fifo->signals), FIF_SIG_DATAIN) == FIF_SIG_DATAIN);

			spin_lock (&(fifo->nr_full.lock));
		}

		fifo->last_out = (fifo->last_out + 1) % fifo->nr_elem;
		offset = fifo->last_out;

		if (baseptr) {
			absptr = (__u8 *)get_abs_ptr (baseptr, fifo->buf);
		} else absptr = fifo->buf;

		/*We are safe to put element from buffer*/
		memcpy (elem, absptr+(offset *fifo->elem_size), fifo->elem_size);

		/*Signal, that there is a new empty element*/
		fifo->nr_full.semval--;
		spin_unlock (&(fifo->nr_full.lock));

		/*Wake at least one producer*/
		cond_signal (&(fifo->signals), FIF_SIG_DATAOUT, INT_MAX);
		return 0;
	}
	return -1;
}

__s8 try_fif_push (struct sctp_fifo *fifo, __u8 *elem, void *baseptr)
{
	__u32 offset;
	__u8 *absptr;

	if (fifo) {
		/*Check, if there are empty elements available*/
		spin_lock (&(fifo->nr_full.lock));
		if (fifo->nr_full.semval >= (__s32)fifo->nr_elem) {
			spin_unlock (&(fifo->nr_full.lock));
			return -6;
		}

		fifo->last_in = (fifo->last_in + 1) % fifo->nr_elem;
		offset = fifo->last_in;

		if (baseptr) {
			absptr = (__u8 *)get_abs_ptr (baseptr, fifo->buf);
		} else absptr = fifo->buf;

		/*We are safe to put element into buffer*/
		memcpy (absptr+(offset * fifo->elem_size), elem, fifo->elem_size);

		/*Signal, that there is a new full element*/
		fifo->nr_full.semval++;
		spin_unlock (&(fifo->nr_full.lock));

		/*Wake at least one consumer*/
		cond_signal (&(fifo->signals), FIF_SIG_DATAIN, INT_MAX);
		return 0;
	}
	return -1;
}

__s8 try_fif_pop (struct sctp_fifo *fifo, __u8 *elem, void *baseptr)
{
	__u32 offset;
	__u8 *absptr;

	if (fifo) {
		/*Check, if there are full elements*/
		spin_lock (&(fifo->nr_full.lock));
		if (fifo->nr_full.semval <= 0) {
			spin_unlock (&(fifo->nr_full.lock));
			return -7;
		}

		fifo->last_out = (fifo->last_out + 1) % fifo->nr_elem;
		offset = fifo->last_out;

		if (baseptr) {
			absptr = (__u8 *)get_abs_ptr (baseptr, fifo->buf);
		} else absptr = fifo->buf;

		/*We are safe to put element from buffer*/
		memcpy (elem, absptr+(offset *fifo->elem_size), fifo->elem_size);

		fifo->nr_full.semval--;
		spin_unlock (&(fifo->nr_full.lock));

		/*Wake at least one producer*/
		cond_signal (&(fifo->signals), FIF_SIG_DATAOUT, INT_MAX);
		return 0;
	}
	return -1;
}

