/*NOTE: CRITICAL sections should be pointers to atomic_var structures (see sctp_atomic.h)
		One should change returned values to corresponding error values (f.e. EAGAIN etc.)*/

#ifndef _SCTP_FIFO
#define _SCTP_FIFO

#include <linux/types.h>
#include "sctp_atomic.h"

#define FIF_SIG_NONE	0
#define FIF_SIG_DATAIN	1
#define FIF_SIG_DATAOUT	2
/*There can be other sigs defined by user now!! :)*/

struct sctp_fifo {
	/*0-63*/
	struct semaphore	signals;		/*Protected signal mask*/
	/*64-127*/
	struct semaphore	nr_full;		/*Number of full elements*/
	/*128-191*/
	__u32	last_out;					/*Offset to element, which was last popped*/
	__u8    pad0[L1D_CLS-4];             /*Padding to seperate last_out, last_in from same cacheline*/
	/*192-255*/
	__u32	last_in;					/*Offset to element, which was last pushed*/
	__u8    pad1[L1D_CLS-4];
	/*256-319*/
	__u32	nr_elem;					/*Number of total elements*/
	__u32	elem_size;					/*Size of one element*/
	__u8    pad2[L1D_CLS-8];
	/*320-4096*/
	__u8	*buf;						/*Pointer to beginning of buffer, which can hold all elements*/
	__u8    pad4[4096-5*L1D_CLS-PTR_SIZE];/*Keep page size alignment*/
} __attribute__ ((packed));

void *get_abs_ptr (void *baseptr, void *relptr);

void *get_rel_ptr (void *baseptr, void *absptr);

/*This funtion will initialize the elements of struct above
 *Returns 0 on success*/
__s8 fif_init (struct sctp_fifo *fifo, __u32 nr, __u32 size);

/*Gets an pointer to buffer and calculates relative pointer if baseptr given (has to be lower)*/
__s8 fif_init_wbuf (struct sctp_fifo *fifo, __u32 nr, __u32 size, __u8 *buf, void *baseptr);

void fif_reset (struct sctp_fifo *fifo);


/*This function pushes an element after last_in, synchronizes with nr_full,nr_empty
 *Returns: >0: Number of full elements <0: Error occured*/
__s8 fif_push (struct sctp_fifo *fifo, __u8 *elem, void *baseptr);

/*This function pops an element out after last_out*/
__s8 fif_pop (struct sctp_fifo *fifo, __u8 *elem, void *baseptr);

/*As above but multiwriter safe*/
/*__s8 mw_fif_push (struct sctp_fifo *fifo, __u8 *elem, void *baseptr);*/

/*As above but multireader safe*/
/*__s8 mr_fif_pop (struct sctp_fifo *fifo, __u8 *elem, void *baseptr);*/


/*Like funcs above, but they wont block/spin on unsuccessful syncs*/
__s8 try_fif_push (struct sctp_fifo *fifo, __u8 *elem, void *baseptr);

__s8 try_fif_pop (struct sctp_fifo *fifo, __u8 *elem, void *baseptr);

/*__s8 mw_try_fif_push (struct sctp_fifo *fifo, __u8 *elem, void *baseptr);*/

/*__s8 mr_try_fif_pop (struct sctp_fifo *fifo, __u8 *elem, void *baseptr);*/


#endif
