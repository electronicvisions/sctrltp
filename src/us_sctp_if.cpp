/*This is the interface implementation for upper Userspace progs for SCTP*/

#include <arpa/inet.h>
#include <assert.h>
#include <sys/file.h>
#include "sctrltp/us_sctp_if.h"

namespace sctrltp {

namespace {

static void *open_shared_mem (const char *NAME, __u32 size)
{
	void *ptr = NULL;
	__s32 fd, ret;

	fd = shm_open (NAME, O_RDWR, 0666);
	if (fd < 0)
	{
		SCTRL_LOG_ERROR("Failed to open shared mem object (NAME: %s)", NAME);
		return NULL;
	}

	ret = flock(fd, LOCK_SH);
	if (ret < 0) {
		SCTRL_LOG_ERROR("Could not get shared lock on shared memory file (NAME: %s)", NAME);
		close(fd);
		return NULL;
	}

	ptr = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED)
	{
		SCTRL_LOG_ERROR("Could not map mem to process space (NAME: %s)", NAME);
		close (fd);
		return NULL;
	}
	assert (((__u64)ptr % 4096) == 0);

	close(fd);
	return ptr;
}

static void close_shared_mem (void *shared_mem, __u32 size)
{
	__s32 ret;
	ret = munmap (shared_mem, size);
	if (ret < 0)
	{
		SCTRL_LOG_ERROR("Unmapping of shared mem region failed!");
	}
	return;
}

template<typename P>
static arq_frame<P> *fetch_frames (sctp_fifo *fifo, sctp_alloc<P> *local_buf, void *baseptr) {
	__u32 i;
	if ((i = local_buf->next) < local_buf->num) {
		/*We have a frame in local cache*/
		local_buf->next++;
		return local_buf->fptr[i];
	} else {
		/*We dont have an unprocessed frame, so lets fetch new ones*/
		fif_pop (fifo, (__u8 *)local_buf, baseptr);
		for (i = 0; i < local_buf->num; i++) {
			local_buf->fptr[i] = static_cast<arq_frame<P>*>(get_abs_ptr (baseptr, local_buf->fptr[i]));
		}
		local_buf->next = 1;
		return local_buf->fptr[0];
	}
}

template<typename P>
static void push_frames (sctp_fifo *fifo, sctp_alloc<P> *local_buf, void *baseptr, arq_frame<P> *ptr, __u8 flush) {
	__u32 i;
	if (!ptr) {
		if (flush && ((i = local_buf->next) > 0)) {
			/*We dont have another frame, but want to flush remaining frames*/
			local_buf->num = i;
			local_buf->next = 0;
			fif_push (fifo, (__u8 *)local_buf, baseptr);
			return;
		}
		/*So there is no remaining frame, do nothing*/
		return;
	} else {
		if ((i = local_buf->next) < PARALLEL_FRAMES) {
			/*There is room in local_buf to check frame in*/
			local_buf->fptr[i] = static_cast<arq_frame<P>*>(get_rel_ptr (baseptr, ptr));
			local_buf->next++;
			if (flush) {
				/*Even if we have not fully filled local_buf, we want to push it ...*/
				local_buf->num = i + 1;
				local_buf->next = 0;
				fif_push (fifo, (__u8 *)local_buf, baseptr);
			}
			return;
		} else {
			/*Local buf totally full, so lets push it up first ...*/
			local_buf->num = PARALLEL_FRAMES;
			local_buf->next = 0;
			fif_push (fifo, (__u8 *)local_buf, baseptr);
			/*... but do not forget to register our frame*/
			local_buf->next = 1;
			local_buf->fptr[0] = static_cast<arq_frame<P>*>(get_rel_ptr (baseptr, ptr));
			return;
		}
	}
}

} // namespace anonymous

/*lower level interface functions*/

template<typename P>
sctp_descr<P> *open_conn (const char *corename)
{
	struct sctp_descr<P> *desc = NULL;
	struct sctp_interface<P> *ptr = NULL;

	/*Creating descriptor and mapping shared memory to own adress space*/
	desc = (sctp_descr<P> *) malloc (sizeof(sctp_descr<P>));
	if (!desc) return NULL;

	memset (desc, 0, sizeof (sctp_descr<P>));

	/*Initialize mutex*/
	mutex_init (&(desc->mutex));

	/*Connect to core shared memory interface*/
	ptr = (sctp_interface<P> *)open_shared_mem ((const char *)corename, sizeof(sctp_interface<P>));
	if (!ptr) {
		free(desc);
		return NULL;
	}

	desc->trans = ptr;
	strncpy ((char *)desc->name, (char *)corename, sizeof(desc->name) - 1);

	return desc;
}

template<typename P>
__s32 close_conn (sctp_descr<P> *desc)
{
	__u32 i,j,k;

	/*Clean local cache(s)*/
	/*release frames of send_buf if necessary*/
	if ((i = desc->send_buf.out.next) > 0) {
		/*there is something old in sending cache, lets recycle it*/
		desc->send_buf.out.num = i;
		desc->send_buf.out.next = 0;
		fif_push (&(desc->trans->alloctx), (__u8 *)&(desc->send_buf.out), desc->trans);
	}

	/*Are there already fetched empty frames in local cache?*/
	if (desc->send_buf.in.next < desc->send_buf.in.num) {
		/*There are unused frames in local cache*/
		/*Transform buffer and calculate relative pointer*/
		i = 0;
		j = desc->send_buf.in.next;
		while (j < desc->send_buf.in.num) {
			desc->send_buf.in.fptr[i] = static_cast<arq_frame<P>*>(get_rel_ptr (desc->trans, desc->send_buf.in.fptr[j]));
			j++;
			i++;
		}
		/*Set up fields*/
		desc->send_buf.in.num = i;
		desc->send_buf.in.next = 0;
		/*push buffer to alloctx*/
		fif_push (&(desc->trans->alloctx), (__u8 *)&(desc->send_buf.in), desc->trans);
	}

	if ((i = desc->recv_buf.out.next) > 0) {
		/*there is something unrecycled, lets recycle it*/
		desc->recv_buf.out.num = i;
		desc->recv_buf.out.next = 0;
		fif_push (&(desc->trans->allocrx), (__u8 *)&(desc->recv_buf.out), desc->trans);
	}

	/*Cycle through recv_buf(fers) and recycle unprocessed frames if necessary*/
	for (k = 0; k < desc->trans->unique_queue_map.size + 1; k++) {
		if (desc->recv_buf.in[k].next < desc->recv_buf.in[k].num) {
			/*There are unused frames in local cache*/
			/*Transform buffer and calculate relative pointer*/
			i = 0;
			j = desc->recv_buf.in[k].next;
			while (j < desc->recv_buf.in[k].num) {
				desc->recv_buf.in[k].fptr[i] = static_cast<arq_frame<P>*>(get_rel_ptr (desc->trans, desc->recv_buf.in[k].fptr[j]));
				j++;
				i++;
			}
			/*Set up fields*/
			desc->recv_buf.in[k].num = i;
			desc->recv_buf.in[k].next = 0;
			/*push buffer to allocrx*/
			fif_push (&(desc->trans->allocrx), (__u8 *)&(desc->recv_buf.in[k]), desc->trans);
		}
	}

	/*Unmap shared mem*/
	close_shared_mem (desc->trans, sizeof(sctp_interface<P>));
	/*free descr*/
	free(desc);
	return 0;
}

template<typename P>
__s32 acq_buf (sctp_descr<P> *desc, buf_desc<P> *acq, const __u8 mode)
{
	__u32 i;
	__s32 ret;
	sctp_fifo *infifo = NULL;
	arq_frame<P> *ptr_to_frame = NULL;
	assert (desc != NULL);
	assert (acq  != NULL);

	/*Get frame ONLY from alloctx never from allocrx*/
	infifo = &(desc->trans->alloctx);

	if (mode & MODE_SAFE) {
		if (mode & MODE_NONBLOCK) {
			if (!mutex_try_lock(&(desc->mutex))) return SC_BUSY;
		} else mutex_lock (&(desc->mutex));
	}

	/*critical section*/
	if ((i = desc->send_buf.in.next) < desc->send_buf.in.num) {
		/*We have a cached frame pointer available*/
		desc->send_buf.in.next++;
		ptr_to_frame = desc->send_buf.in.fptr[i];
	} else {
		/*We have to acquire new frame pointer(s) from lower layer*/
		if (mode & MODE_NONBLOCK) {
			/*non blocking IO*/
			if ((ret = try_fif_pop (infifo, (__u8 *)&(desc->send_buf.in), desc->trans)) < 0) {
				if (mode & MODE_SAFE) mutex_unlock (&(desc->mutex));
				return ret;
			}
		} else fif_pop (infifo, (__u8 *)&(desc->send_buf.in), desc->trans);

		for (i = 0; i < desc->send_buf.in.num; i++) {
			desc->send_buf.in.fptr[i] = static_cast<arq_frame<P>*>(get_abs_ptr (desc->trans, desc->send_buf.in.fptr[i]));
		}

		desc->send_buf.in.next = 1;
		ptr_to_frame = desc->send_buf.in.fptr[0];
	}
	/*end of critical section*/

	if (mode & MODE_SAFE) mutex_unlock (&(desc->mutex));

	assert (ptr_to_frame >= desc->trans->pool);

	/*Update pointer fields of buf_desc to point to newly acquired buffer*/
	acq->arq_sctrl = ptr_to_frame;
#if (__GNUC__ >= 9)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif
	acq->payload = acq->arq_sctrl->COMMANDS;
#if (__GNUC__ >= 9)
#pragma GCC diagnostic pop
#endif

	return 1;
}

template<typename P>
__s32 rel_buf (sctp_descr<P> *desc, buf_desc<P> *rel, const __u8 mode)
{
	__u32 i;
	__s32 ret;

	assert (desc != NULL);
	if ((!rel) && !(mode & MODE_FLUSH)) return SC_INVAL;

	if (mode & MODE_SAFE) {
		if (mode & MODE_NONBLOCK) {
			if (!mutex_try_lock(&(desc->mutex))) return SC_BUSY;
		} else mutex_lock (&(desc->mutex));
	}

	/*critical section*/

	if (rel) {
		if (mode & MODE_TX) {
			/*Put frame pointer back to local cache, because it was once released from there*/
			/*TODO: Make this safer*/
			assert(desc->send_buf.in.next != 0);
			i = --desc->send_buf.in.next;
			desc->send_buf.in.fptr[i] = rel->arq_sctrl;
		} else {
			if ((i = desc->recv_buf.out.next) < PARALLEL_FRAMES) {
				/*There is room in cache to put pointer in*/
				desc->recv_buf.out.fptr[i] = static_cast<arq_frame<P>*>(get_rel_ptr (desc->trans, rel->arq_sctrl));
				desc->recv_buf.out.next++;
			} else {
				/*We have one entry full, so lets release it first ...*/
				desc->recv_buf.out.num = PARALLEL_FRAMES;
				desc->recv_buf.out.next = 0;
				if (mode & MODE_NONBLOCK) {
					/*non blocking IO*/
					if ((ret = try_fif_push (&(desc->trans->allocrx), (__u8 *)&(desc->recv_buf.out), desc->trans)) < 0) {
						if (mode & MODE_SAFE) mutex_unlock (&(desc->mutex));
						return ret;
					}
				} else fif_push (&(desc->trans->allocrx), (__u8 *)&(desc->recv_buf.out), desc->trans);
				/*... and then register our pointer in a fresh entry*/
				desc->recv_buf.out.next = 1;
				desc->recv_buf.out.fptr[0] = static_cast<arq_frame<P>*>(get_rel_ptr (desc->trans, rel->arq_sctrl));
			}
		}
	}

	if ((mode & MODE_FLUSH) && ((i = desc->recv_buf.out.next) > 0)) {
		/*there is something to flush in cache*/
		desc->recv_buf.out.num = i;
		desc->recv_buf.out.next = 0;
		if (mode & MODE_NONBLOCK) {
			/*non blocking IO*/
			if ((ret = try_fif_push (&(desc->trans->allocrx), (__u8 *)&(desc->recv_buf.out), desc->trans)) < 0) {
				if (mode & MODE_SAFE) mutex_unlock (&(desc->mutex));
				return ret;
			}
		} else fif_push (&(desc->trans->allocrx), (__u8 *)&(desc->recv_buf.out), desc->trans);
	}

	/*end of critical section*/

	if (mode & MODE_SAFE) mutex_unlock (&(desc->mutex));

	if (rel) memset (rel, 0, sizeof (buf_desc<P>));

	return 1;
}

template<typename P>
__s32 send_buf (sctp_descr<P> *desc, buf_desc<P> *buf, const __u8 mode)
{
	__u32 i;
	__s32 ret;
	__u8 do_wake = 0;

	assert (desc != NULL);
	if (!buf && !(mode & MODE_FLUSH)) return SC_INVAL;

	if (mode & MODE_SAFE) {
		if (mode & MODE_NONBLOCK) {
			if (!mutex_try_lock(&(desc->mutex))) return SC_BUSY;
		} else mutex_lock (&(desc->mutex));
	}
	/*critical section*/


	if (buf) {

		/*We have to register a buffer to pass to lower layer*/
		if ((i = desc->send_buf.out.next) < PARALLEL_FRAMES) {
			desc->send_buf.out.fptr[i] = static_cast<arq_frame<P>*>(get_rel_ptr (desc->trans, buf->arq_sctrl));
			desc->send_buf.out.next++;
		} else {
			desc->send_buf.out.num = PARALLEL_FRAMES;
			desc->send_buf.out.next = 0;
			if (mode & MODE_NONBLOCK) {
				/*non blocking IO*/
				if ((ret = try_fif_push (&(desc->trans->tx_queue), (__u8 *)&(desc->send_buf.out), desc->trans)) < 0) {
					if (mode & MODE_SAFE) mutex_unlock (&(desc->mutex));
					return ret;
				}
			} else fif_push (&(desc->trans->tx_queue), (__u8 *)&(desc->send_buf.out), desc->trans);
			do_wake = 1;
			desc->send_buf.out.next = 1;
			desc->send_buf.out.fptr[0] = static_cast<arq_frame<P>*>(get_rel_ptr (desc->trans, buf->arq_sctrl));
		}
	}

	if ((mode & MODE_FLUSH) && ((i = desc->send_buf.out.next) > 0)) {
		/*there is something to flush in cache*/
		desc->send_buf.out.num = i;
		desc->send_buf.out.next = 0;
		if (mode & MODE_NONBLOCK) {
			/*non blocking IO*/
			if ((ret = try_fif_push (&(desc->trans->tx_queue), (__u8 *)&(desc->send_buf.out), desc->trans)) < 0) {
				if (mode & MODE_SAFE) mutex_unlock (&(desc->mutex));
				return ret;
			}
		} else fif_push (&(desc->trans->tx_queue), (__u8 *)&(desc->send_buf.out), desc->trans);
		do_wake = 1;
	}
	/*end of critical section*/

	if (mode & MODE_SAFE) mutex_unlock (&(desc->mutex));

	if (buf) memset (buf, 0, sizeof(buf_desc<P>));

	/*We need to wake a possible sleeping TX thread, if data was passed to him*/
	if (do_wake) cond_signal (&(desc->trans->waketx), 1, 1);

	return 1;
}

template<typename P>
__s32 tx_send_buf_empty (sctp_descr<P> *desc)
{
	/* Is there something in the send cache? */
	if (desc->send_buf.out.next > 0) {
		return 0;
	} else {
		return 1;
	}
}

template<typename P>
__s32 tx_queue_empty (sctp_descr<P> *desc)
{
	assert (desc != NULL);
	if (desc->trans->tx_queue.nr_full.semval != 0)
		return 0;
	return 1; // true
}


template<typename P>
__s32 tx_queue_full (sctp_descr<P> *desc)
{
	assert (desc != NULL);
	if (desc->trans->tx_queue.nr_full.semval < (int)desc->trans->tx_queue.nr_elem)
		return 0;
	return 1; // true
}

template<typename P>
__s32 rx_recv_buf_empty (sctp_descr<P> *desc)
{
    if (desc->recv_buf.in[0].next < desc->recv_buf.in[0].num) {
        return 0;
    } else {
        return 1; // true
    }
}

template<typename P>
__s32 rx_recv_buf_empty (sctp_descr<P> *desc, __u64 idx)
{
    __u64 queue = 1 + idx;
    if (desc->recv_buf.in[queue].next < desc->recv_buf.in[queue].num) {
        return 0;
    } else {
        return 1; // true
    }
}

template<typename P>
__s32 rx_queue_empty (sctp_descr<P> *desc)
{
	assert (desc != NULL);
	if (desc->trans->rx_queues[0].nr_full.semval != 0)
		return 0;
	return 1; // true
}

template<typename P>
__s32 rx_queue_empty (sctp_descr<P> *desc, __u64 idx)
{
    assert (desc != NULL);
    assert (idx < desc->trans->unique_queue_map.size);
    if (desc->trans->rx_queues[idx + 1].nr_full.semval != 0)
        return 0;
    return 1; // true
}

template<typename P>
__s32 rx_queue_full (struct sctp_descr<P> *desc)
{
	assert (desc != NULL);
	if (desc->trans->rx_queues[0].nr_full.semval < (int)desc->trans->rx_queues[0].nr_elem)
		return 0;
	return 1; // true
}

template<typename P>
__s32 rx_queue_full (sctp_descr<P> *desc, __u64 idx)
{
	assert (desc != NULL);
	assert (idx < desc->trans->unique_queue_map.size);
	if (desc->trans->rx_queues[idx + 1].nr_full.semval < (int)desc->trans->rx_queues[idx + 1].nr_elem)
		return 0;
	return 1; // true
}

template<typename P>
__s32 recv_buf (sctp_descr<P> *desc, buf_desc<P> *buf, const __u8 mode)
{
	__u32 i;
	__s32 ret;
	arq_frame<P> *ptr_to_frame = NULL;

	assert (desc != NULL);
	assert (buf != NULL);

	if (mode & MODE_SAFE) {
		if (mode & MODE_NONBLOCK) {
			if (!mutex_try_lock(&(desc->mutex))) return SC_BUSY;
		} else mutex_lock (&(desc->mutex));
	}
	/*critical section*/

	i = desc->recv_buf.in[0].next;
	if (!rx_recv_buf_empty(desc)) {
		desc->recv_buf.in[0].next++;
		ptr_to_frame = desc->recv_buf.in[0].fptr[i];
	} else {
		if (mode & MODE_NONBLOCK) {
			/*non blocking IO*/
			if ((ret = try_fif_pop(
			         &(desc->trans->rx_queues[0]), (__u8*) &(desc->recv_buf.in[0]), desc->trans)) <
			    0) {
				if (mode & MODE_SAFE)
					mutex_unlock(&(desc->mutex));
				return ret;
			}
		} else
			fif_pop(&(desc->trans->rx_queues[0]), (__u8*) &(desc->recv_buf.in[0]), desc->trans);

		for (i = 0; i < desc->recv_buf.in[0].num; i++) {
			desc->recv_buf.in[0].fptr[i] =
			    static_cast<arq_frame<P>*>(get_abs_ptr(desc->trans, desc->recv_buf.in[0].fptr[i]));
		}

		desc->recv_buf.in[0].next = 1;
		ptr_to_frame = desc->recv_buf.in[0].fptr[0];
	}

	/*end of critical section*/
	if (mode & MODE_SAFE)
		mutex_unlock(&(desc->mutex));

	/*Update pointer fields in buf_desc*/
	buf->arq_sctrl = ptr_to_frame;
#if (__GNUC__ >= 9)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif
	buf->payload = buf->arq_sctrl->COMMANDS;
#if (__GNUC__ >= 9)
#pragma GCC diagnostic pop
#endif

	return 1;
}

template <typename P>
__s32 get_next_frame_pid(sctp_descr<P>* desc)
{
	assert(desc != NULL);
	arq_frame<P>* ptr_to_frame = NULL;
	if (desc->recv_buf.in[0].next < desc->recv_buf.in[0].num) {
		ptr_to_frame = desc->recv_buf.in[0].fptr[desc->recv_buf.in[0].next];
	} else {
		sctp_alloc<P>* alloc = NULL;
		fif_front(&(desc->trans->rx_queues[0]), (__u8*) &(alloc), desc->trans);
		ptr_to_frame = static_cast<arq_frame<P>*>(get_abs_ptr(desc->trans, alloc->fptr[0]));
	}
	return ntohs(ptr_to_frame->PTYPE);
}

template <typename P>
__s32 recv_buf(sctp_descr<P>* desc, buf_desc<P>* buf, const __u8 mode, __u64 idx)
{
	__u32 i;
	__s32 ret;
	__u64 queue;
	arq_frame<P>* ptr_to_frame = NULL;

	assert(desc != NULL);
	assert(buf != NULL);
	assert(idx < desc->trans->unique_queue_map.size);
	queue = idx + 1;

	if (mode & MODE_SAFE) {
		if (mode & MODE_NONBLOCK) {
			if (!mutex_try_lock(&(desc->mutex)))
				return SC_BUSY;
		} else
			mutex_lock(&(desc->mutex));
	}
	/*critical section*/

	i = desc->recv_buf.in[queue].next;
	if (!rx_recv_buf_empty(desc, idx)) {
		desc->recv_buf.in[queue].next++;
		ptr_to_frame = desc->recv_buf.in[queue].fptr[i];
	} else {
		if (mode & MODE_NONBLOCK) {
			/*non blocking IO*/
			if ((ret = try_fif_pop (&(desc->trans->rx_queues[queue]), (__u8 *)&(desc->recv_buf.in[queue]), desc->trans)) < 0) {
				if (mode & MODE_SAFE) mutex_unlock (&(desc->mutex));
				return ret;
			}
		} else fif_pop (&(desc->trans->rx_queues[queue]), (__u8 *)&(desc->recv_buf.in[queue]), desc->trans);

		for (i = 0; i < desc->recv_buf.in[queue].num; i++) {
			desc->recv_buf.in[queue].fptr[i] = static_cast<arq_frame<P>*>(get_abs_ptr (desc->trans, desc->recv_buf.in[queue].fptr[i]));
		}

		desc->recv_buf.in[queue].next = 1;
		ptr_to_frame = desc->recv_buf.in[queue].fptr[0];
	}

	/*end of critical section*/
	if (mode & MODE_SAFE) mutex_unlock (&(desc->mutex));

	/*Update pointer fields in buf_desc*/
	buf->arq_sctrl = ptr_to_frame;
#if (__GNUC__ >= 9)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif
	buf->payload = buf->arq_sctrl->COMMANDS;
#if (__GNUC__ >= 9)
#pragma GCC diagnostic pop
#endif

	return 1;
}

template<typename P>
__s32 init_buf (buf_desc<P> *buf)
{
	assert (buf != NULL);
	buf->arq_sctrl->LEN = 0;
	buf->arq_sctrl->PTYPE = 0;
	return 1;
}

template<typename P>
__s32 append_words (buf_desc<P> *buf, const __u16 ptype, const __u32 num, const __u64 *values)
{
	__u32 curr_typ;
	__u32 curr_num;
	__u32 max_num;
	__u32 count = 0;
	__u64 *ptr;

	/*Check given arguments on validity*/
	assert (buf != NULL);

	ptr = buf->payload;
	curr_num = ntohs(buf->arq_sctrl->LEN);

	/* fresh frame? set ptype :) */
	if (curr_num == 0)
		buf->arq_sctrl->PTYPE = htons(ptype); // FIXME

	curr_typ = ntohs(buf->arq_sctrl->PTYPE);

	/* check if packet already too long, this must not happen ;) */
	assert (curr_num <= P::MAX_PDUWORDS);
	
	/* can only append if packet type matches */
	if (curr_typ != ptype) {
		SCTRL_LOG_ERROR("Cannot append to frame; ptype changed!");
		return SC_CORRUPT;
	}

	max_num = curr_num + num;
	if (max_num > P::MAX_PDUWORDS)
		max_num = P::MAX_PDUWORDS;
	count = max_num - curr_num;

	/* append as many words to packet as possible */
	if (values)
		memcpy(ptr+curr_num, values, count * WORD_SIZE);
	else
		memset(ptr+curr_num, 0, count * WORD_SIZE);
	buf->arq_sctrl->LEN = htons(max_num);

	return count;
}

/*Older but easier to use interface functions*/

template<typename P>
sctp_descr<P> *SCTP_Open (const char *corename)
{
	return open_conn<P>(corename);
}

template<typename P>
__s32 SCTP_Close (sctp_descr<P> *desc)
{
	return close_conn (desc);
}

/*This function pushes packet(s) to tx_queue of Core (COULD BLOCK INFINITELY IF NOONE READS RESPONSES)*/
template<typename P>
__s64 SCTP_Send (sctp_descr<P> *desc, const __u16 typ, const __u32 num, const __u64 *payload)
{
	__u64 const *cmd;
	arq_frame<P> *packet = NULL;
	arq_frame<P> *sc_packet;
	__u64 *sc_cmd;
	__u32 i;
	__u8 j;

	if (!desc)
		return -1;

	i = 0;
	cmd = payload;
	/*Loop through given buffer and put commands into frame (auto packeting, could hang if nothing is read from rx_queue)*/
	while (i < num) {
		packet = fetch_frames (&(desc->trans->alloctx), &(desc->send_buf.in), desc->trans);

		sc_packet = packet;
#if (__GNUC__ >= 9)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif
		sc_cmd = sc_packet->COMMANDS;
#if (__GNUC__ >= 9)
#pragma GCC diagnostic pop
#endif
		if ((num - i) >= P::MAX_PDUWORDS)
			j = P::MAX_PDUWORDS;
		else
			j = num - i;
		/*Set header of frame (NOTE: global bit is set automatically according to payload and nathannr)*/
		sctpreq_set_header (packet, j, typ);
		memcpy (sc_cmd, &cmd[i], sizeof (__u64) * j);
		i += j;

		push_frames<P> (&(desc->trans->tx_queue), &(desc->send_buf.out), desc->trans, packet, 0);
	}
	/*Flush any remaining frame(s)*/
	push_frames<P> (&(desc->trans->tx_queue), &(desc->send_buf.out), desc->trans, NULL, 1);
	/*Wake TX*/
	cond_signal (&(desc->trans->waketx), 1, 1);

	return num;
}

/* Pulls one packet from rx_queue associated with nathan and gives amount back
 * (data will be copied in a buffer).
 */
template<typename P>
__s32 SCTP_Recv (sctp_descr<P> *desc, __u16 *typ, __u16 *num, __u64 *resp)
{
 	arq_frame<P> *respons;
	arq_frame<P> *tmp2;
	int queue = 0;

	if (!desc | !resp | !typ | !num)
		return -1;

	respons = fetch_frames (&(desc->trans->rx_queues[queue]), &(desc->recv_buf.in[queue]), desc->trans);

	tmp2 = respons;

	/*Pass nathan, sctrl flags, and responses to user*/
	*num = ntohs(tmp2->LEN);
	*typ = ntohs(tmp2->PTYPE);

	memcpy (resp, tmp2->COMMANDS, sizeof (__u64) * (*num));

	/*Now dump frame (recycle it)*/
	push_frames<P> (&(desc->trans->allocrx), &(desc->recv_buf.out), desc->trans, respons, 0);

	return 0;
}

#define PARAMETERISATION(Name, name)                                                               \
	template sctp_descr<Name>* open_conn(const char* corename);                                    \
	template __s32 close_conn(sctp_descr<Name>* desc);                                             \
	template __s32 acq_buf(sctp_descr<Name>* desc, buf_desc<Name>* acq, const __u8 mode);          \
	template __s32 rel_buf(sctp_descr<Name>* desc, buf_desc<Name>* rel, const __u8 mode);          \
	template __s32 send_buf(sctp_descr<Name>* desc, buf_desc<Name>* buf, const __u8 mode);         \
	template __s32 recv_buf(sctp_descr<Name>* desc, buf_desc<Name>* buf, const __u8 mode);         \
	template __s32 get_next_frame_pid(sctp_descr<Name>* desc);                                     \
	template __s32 recv_buf(sctp_descr<Name>* desc, buf_desc<Name>* buf, __u8 mode, __u64 idx);    \
	template __s32 init_buf(buf_desc<Name>* buf);                                                  \
	template __s32 append_words(                                                                   \
	    buf_desc<Name>* buf, const __u16 ptype, const __u32 num, const __u64* values);             \
	template __s32 rx_recv_buf_empty(sctp_descr<Name>* desc);                                      \
	template __s32 rx_recv_buf_empty(sctp_descr<Name>* desc, __u64 idx);                           \
	template __s32 tx_queue_empty(sctp_descr<Name>* desc);                                         \
	template __s32 tx_queue_full(sctp_descr<Name>* desc);                                          \
	template __s32 tx_send_buf_empty(sctp_descr<Name>* desc);                                      \
	template __s32 rx_queue_empty(sctp_descr<Name>* desc);                                         \
	template __s32 rx_queue_full(sctp_descr<Name>* desc);                                          \
	template __s32 rx_queue_empty(sctp_descr<Name>* desc, __u64 idx);                              \
	template __s32 rx_queue_full(sctp_descr<Name>* desc, __u64 idx);                               \
	template sctp_descr<Name>* SCTP_Open(const char* corename);                                    \
	template __s32 SCTP_Close(sctp_descr<Name>* desc);                                             \
	template __s64 SCTP_Send(                                                                      \
	    sctp_descr<Name>* desc, const __u16 typ, const __u32 num, const __u64* payload);           \
	template __s32 SCTP_Recv(sctp_descr<Name>* desc, __u16* typ, __u16* num, __u64* resp);
#include "sctrltp/parameters.def"

} // namespace sctrltp
