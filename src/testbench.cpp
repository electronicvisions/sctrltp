/*Testbench for SCTP-Core*/

#include <assert.h>
#include <sys/stat.h>
#include <stdio.h>
#include <linux/types.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "sctrltp/us_sctp_if.h"
#include "sctrltp/us_sctp_defs.h"
#include "sctrltp/packets.h"
#include "sctrltp/sctp_fifo.h"

using namespace sctrltp;

template <typename P>
static sctp_descr<P>*& get_desc()
{
	static sctp_descr<P> *desc = NULL;
	return desc;
}

template <typename P>
static struct arq_frame<P> *fetch_frames (struct sctp_fifo *fifo, struct sctp_alloc<P> *local_buf, void *baseptr) {
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

template <typename P>
static void push_frames (struct sctp_fifo *fifo, struct sctp_alloc<P> *local_buf, void *baseptr, struct arq_frame<P> *ptr, __u8 flush) {
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

int main (int argc, char **argv)
{
	sctp_alloc<ParametersFcpBss1> inbuf_rx;
	sctp_alloc<ParametersFcpBss1> outbuf_rx;
	sctp_alloc<ParametersFcpBss1> inbuf_tx;
	sctp_alloc<ParametersFcpBss1> outbuf_tx;

	arq_frame<ParametersFcpBss1> *ptr;
	arq_frame<ParametersFcpBss1> *ptr_out;
	arq_frame<ParametersFcpBss1> *sc_header;
	__u64 *sc_cmds;
	__u64 *sc_resps;
	arq_frame<ParametersFcpBss1> *sc_header_out;

	/*Variables to simulate nathan with module*/
	__u32 i;

	int fd=-1;

	int queue = 0;

	memset (&inbuf_rx, 0, sizeof(sctp_alloc<ParametersFcpBss1>));
	memset (&inbuf_tx, 0, sizeof(sctp_alloc<ParametersFcpBss1>));
	memset (&outbuf_rx, 0, sizeof(sctp_alloc<ParametersFcpBss1>));
	memset (&outbuf_tx, 0, sizeof(sctp_alloc<ParametersFcpBss1>));

#ifdef WITH_ROUTING
	if (argc < 3) {
		printf ("Usage: %s <corename> <nathannr> ([logfile])\n", argv[0]);
		printf ("\t <nathannr> defines ID of nathan to simulate (range: 0-16)\n");
		return 1;
	}
#else
	if (argc < 2) {
		printf ("Usage: %s <corename> ([logfile])\n", argv[0]);
		return 1;
	}
#endif

	if (argc > 3) {
		fd = open (argv[2], O_CREAT | O_WRONLY, 0666);
		if (fd<0) {
			printf ("ERROR: Could not create file for writing\n");
			return 1;
		}
	}

	printf ("Testbench init\n");

	printf ("Connecting to Core...\n");

	get_desc<Parameters<>>() = SCTP_Open<Parameters<>> (argv[1]);
	if (!get_desc<Parameters<>>()) {
		fprintf (stderr, "Error: Could not connect to core\n");
		return 1;
	}

	printf ("Listening for packets\n");

	while (1) {
		/*Fetch empty buffer from alloctx*/
		ptr_out = fetch_frames (&(get_desc<Parameters<>>()->trans->alloctx), &inbuf_tx, get_desc<Parameters<>>()->trans);

		/*Fetch frame from rx_queue*/
		ptr = fetch_frames (&(get_desc<Parameters<>>()->trans->rx_queues[queue]), &inbuf_rx, get_desc<Parameters<>>()->trans);

		/*Handle frame*/
		sc_header = ptr;
#if (__GNUC__ >= 9)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif
		sc_cmds = sc_header->COMMANDS;

		sc_header_out = ptr_out;
		sc_resps = sc_header_out->COMMANDS;
#if (__GNUC__ >= 9)
#pragma GCC diagnostic pop
#endif

		/*Process content of recv. packet*/
		sc_header_out->LEN = sc_header->LEN;

		for (i = 0; i < sc_header->LEN; i++) {
			// loop it back
			sc_resps[i] = sc_cmds[i];
		}

		/*Recycle frame from rx_queue*/
		push_frames (&(get_desc<Parameters<>>()->trans->allocrx), &outbuf_rx, get_desc<Parameters<>>()->trans, ptr, 1);

		/*Push response frame to tx_queue*/
		push_frames (&(get_desc<Parameters<>>()->trans->tx_queue), &outbuf_tx, get_desc<Parameters<>>()->trans, ptr_out, 1);

		/*Wake TX*/
		cond_signal (&(get_desc<Parameters<>>()->trans->waketx), 1, 1);
	}

	return 0;
}
