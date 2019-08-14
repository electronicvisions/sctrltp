/**/

#include <errno.h>
#include <time.h>
#include <sched.h>
#include "sctrltp/us_sctp_sock.h"
#include <sys/time.h>

double mytime() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return 1.0 * now.tv_sec + now.tv_usec / 1e6;
}
__s8 sock_init (struct sctp_sock *ssock, const __u32 *remote_ip)
{
	struct sockaddr_in opts;
	struct in_addr rip; // ECM: remote ip TODO: add to arguments
	int retval, sock_buf_size;
	(void)retval; // ECM(2017-12-07): We use retval in some code paths...
#ifdef WITH_PACKET_MMAP
	unsigned int i, j;
#endif
#ifdef WITH_BPF
	struct bpf_program filter;
	struct bpf_insn prog[] = {
		/*Check on protocol number*/
		BPF_STMT(BPF_LD+BPF_H+BPF_ABS, 12),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, 0, 0, 11),
		/*Check on my MAC*/
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, 0),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, 0, 0, 9),
		BPF_STMT(BPF_LD+BPF_H+BPF_ABS, 4),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, 0, 0, 7),
		/*Check on remote MAC*/
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, 6),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, 0, 0, 5),
		BPF_STMT(BPF_LD+BPF_H+BPF_ABS, 10),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, 0, 0, 3),
		/*Check on arq protocol type*/
		BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 15),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, SCTP_TYP_DEFAULT, 0, 1),
		/*accept packet by returning -1*/
		BPF_STMT(BPF_RET+BPF_K, -1),
		/*ignore/drop packet by returning 0*/
		BPF_STMT(BPF_RET+BPF_K, 0),
	};
	/*Register instructions in program structure*/
	filter.bf_insns = prog;
	/*Insert arguments to set up filter properly*/
	prog[1].k = proto;
	prog[3].k = ((__u32)txmac[0]<<24)+((__u32)txmac[1]<<16)+((__u32)txmac[2]<<8)+txmac[3];
	prog[5].k = ((__u16)txmac[4]<<8)+txmac[5];
	prog[7].k = ((__u32)rxmac[0]<<24)+((__u32)rxmac[1]<<16)+((__u32)rxmac[2]<<8)+rxmac[3];
	prog[9].k = ((__u16)rxmac[4]<<8)+rxmac[5];
	/*Calculate number of instructions in prog*/
	filter.bf_len = sizeof(prog) / sizeof(struct bpf_insn);
#endif

	memset (&rip, 0, sizeof(struct in_addr));
	memset (ssock, 0, sizeof(struct sctp_sock));

	/*Opening raw socket for sending/receiving*/
	ssock->sd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP); // ECM: changed to DGRAM(=UDP) for stage2 usage
	if (ssock->sd < 0) {
		perror ("socket creation failed");
		return SC_ABORT;
	}

	sock_buf_size = 10 * 1024 * 1024; /* 10 MB of socket buffer */
	retval = setsockopt (ssock->sd, SOL_SOCKET, SO_SNDBUF, (char *)&sock_buf_size, sizeof(sock_buf_size));
	assert (!retval);
	retval = setsockopt (ssock->sd, SOL_SOCKET, SO_RCVBUF, (char *)&sock_buf_size, sizeof(sock_buf_size));
	assert (!retval);


	memcpy(&(ssock->remote_ip), remote_ip, sizeof(__u32)); // remote address

#ifdef WITH_BPF
#error FIX BPF before compiling with BPF
	/*Create Berkeley Packet Filter program and attach it to socket*/
	if (setsockopt(ssock->sd, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter)) != 0) {
		perror ("bpf filter prog attach");
		return SC_ABORT;
	}
#endif

	/* setup data socket: bind to some local port on 0.0.0.0 */
	memset(&opts, 0, sizeof(opts));
	opts.sin_family = AF_INET;
	opts.sin_port = htons(0); // let OS choose our listening port
	opts.sin_addr.s_addr = INADDR_ANY;
	if (bind (ssock->sd, (struct sockaddr *)&opts, sizeof(opts)) < 0) {
		perror ("binding local address failed");
		return SC_ABORT;
	}

	/* "connect" data socket to REMOTE_IP:UDP_DATA_PORT */
	memset(&opts, 0, sizeof(opts));
	opts.sin_family = AF_INET;
	opts.sin_port = htons(UDP_DATA_PORT); /* remote port */
	opts.sin_addr.s_addr = ssock->remote_ip; /* remote address */
	if (connect (ssock->sd, (struct sockaddr *)&opts, sizeof(opts)) != 0) {
		perror ("connecting failed: ");
		return SC_ABORT;
	}


#ifdef WITH_PACKET_MMAP
	/* buffer indices init */
	ssock->ring_idx = 0;

	/* poll structure init */
	ssock->pfd.fd      = ssock->sd;
	ssock->pfd.revents = 0;
	ssock->pfd.events  = POLLIN|POLLERR;

	/* get block size limit: getpagesize() << MAX_ORDER */
	/* maximum get_free_pages is assumed to be 4096 << 11 == 8 MB (2.6/i386 default) */
	ssock->req.tp_block_size = TP_BLOCK_SIZE;
	ssock->req.tp_frame_size = TP_FRAME_SIZE;
	ssock->req.tp_block_nr   = TP_BLOCK_NR;
	ssock->req.tp_frame_nr   = TP_FRAME_NR;
	printf ("PAGE_SIZE is %ld. MAX_ORDER is %d\n", PAGE_SIZE, MAX_ORDER);
	printf ("rx ring: %d %d %d %d\n", TP_BLOCK_SIZE, TP_FRAME_SIZE, TP_BLOCK_NR, TP_FRAME_NR);
	retval = setsockopt (ssock->sd, SOL_PACKET, PACKET_RX_RING, (void *) &(ssock->req), sizeof(ssock->req));
	if (retval) {
		perror("Hmm, some problem with rx ring setup");
		return SC_ABORT;
	}

	/* memory map kernel rx ring buffer into our userspace */
	printf ("Allocating %d bytes of RX Ring memory\n", TP_BLOCK_SIZE * TP_BLOCK_NR);
	ssock->ring_ptr = (void *) mmap (0, TP_BLOCK_SIZE * TP_BLOCK_NR, PROT_READ | PROT_WRITE, MAP_SHARED, ssock->sd, 0);
	/* TODO: If 64-bit cleanness issues arise, check for TPACKET_V2 stuff */
	if (ssock->ring_ptr == MAP_FAILED) {
		perror("Couldn't map RX_RING kernel memory into userspace");
		return SC_ABORT;
	}

	printf ("PACKET_RX_RING activated.\n");

	printf ("%p\n", ssock->ring_ptr);
	ssock->ring = (void *) malloc( sizeof(void *) * ssock->req.tp_frame_nr);
	for (i = ssock->ring_cnt = 0; i < ssock->req.tp_block_nr; i++)
		for (j = 0; j < ssock->req.tp_block_size / ssock->req.tp_frame_size; j++) {
			ssock->ring[ssock->ring_cnt] = (__u8 *) ssock->ring_ptr + i * ssock->req.tp_block_size + j * ssock->req.tp_frame_size;
			printf ("%d %d: %p\n", i, j, ssock->ring[ssock->ring_cnt]);
			ssock->ring_cnt++;
		}

	printf ("Set up %u bytes ring buffer (%u packets)\n", ssock->req.tp_block_size * ssock->req.tp_block_nr, ssock->ring_cnt);

	/* debugging rx ring */
	debug = ssock;
#endif

#ifdef DEBUG
	ssock->debug_fd = open("core.log", O_WRONLY | O_CREAT | O_TRUNC);
	assert(ssock->debug_fd >= 0);
#endif

	return 0;
}


/*returns number of bytes stored into buf (should be HEADER+NB*SCTRLCMD bytes)*/
__s32 sock_read (struct sctp_sock *ssock, struct arq_frame *buf, __u8 filter)
{

	__s32 nread = 0;
	__u8 *tmp;
#ifdef WITH_PACKET_MMAP
	struct tpacket_hdr *hdr; /* pointer to a packet header */
	int retval, loop;
#endif
	tmp = (__u8 *)buf;

	(void) filter; /* TODO: unused parameter */

#ifdef WITH_PACKET_MMAP
	/* try to read packets || poll for packets */


check_rx_ring:
	loop = 1;
	while (loop == 1) { /* prepared to read multiple in a row... */
		/* rx ring structure */
		tmp = ssock->ring[ssock->ring_idx];
		hdr = (struct tpacket_hdr *) tmp;

		/* jump over packet processing => we assume that the kernel inserts at next position! */
		if (! hdr->tp_status)
			break;

		/* goto to next slot in ring */
		if (++ssock->ring_idx >= ssock->ring_cnt)
			ssock->ring_idx = 0;

		/* we do not handle packets that are too big, dropped, other stuff */
		if ((hdr->tp_status & TP_STATUS_COPY))         assert (0); /* return SC_ABORT; */
		if ((hdr->tp_status & TP_STATUS_LOSING))       ssock->drops++;
		if ((hdr->tp_status & TP_STATUS_CSUMNOTREADY)) assert (0); /* return SC_ABORT; */

		/* process packet */
		if (likely(hdr->tp_len == hdr->tp_snaplen)) {

			/* let tmp point to real data: tp_mac + 14 should be tp_net */
			nread = hdr->tp_snaplen;
			tmp = tmp + hdr->tp_mac;

			/* TODO: for now, we copy for upper layers and release slot
			 * => later: handle memory up and call /free-slot/ from upper layer */
			memcpy (buf, tmp, nread);

			/* received a frame - leave the loop (single frame read ;)) */
			loop = 0;
		} else { /* discard the packet */ ; }

		/* release frame */
		hdr->tp_status = TP_STATUS_KERNEL;

		printf("rx: f%2d - read %4d\n", ssock->ring_idx, nread);
	}

	if (unlikely(! nread)) {
		/* poll_for_packet */

		/* we don't want to burn cpu cycles => poll for new packet indefinitetly */
		do {
			/* profiling interrupts system calls, so let's loop it */
			retval = poll(&ssock->pfd, 1, -1);
		} while (retval < 0 && errno == EINTR);

		assert (retval >= 0); /* 0 == timeout, negative indicates error */
		goto check_rx_ring;
	}

#else /* end of WITH_PACKET_MMAP */
	nread = read (ssock->sd, tmp, sizeof(struct arq_frame));
	if (nread < 0) {
		LOG_ERROR("Failed to read from socket: %s\n", strerror(errno));
		return SC_ABORT;
	} else if (nread == 0) {
		LOG_ERROR("Read 0 bytes from socket!?\n");
		return SC_ABORT;
	}
#endif

	return nread;
}

#ifdef WITH_PACKET_MMAP
void print_sock_stats () {
	struct tpacket_hdr *hdr; /* pointer to a packet header */
	__u8 *tmp;
	struct tpacket_stats st;
	socklen_t sts;
	int i, sum = 0;

	printf("looking at f%d (racy ;))\n", debug->ring_idx);
	for (i = 0; i < debug->ring_cnt; ++i) {
		tmp = debug->ring[i];
		hdr = (struct tpacket_hdr *) tmp;

		if (hdr->tp_status) {
			printf("f%d (%lu), ", debug->ring_idx, hdr->tp_status);
			sum++;
		}
	}
	printf("\n");
	sts = sizeof(st);
	if (!getsockopt(debug->sd, SOL_PACKET, PACKET_STATISTICS, (char *) &st, &sts))
		printf("recieved %u packets, dropped %u\n", st.tp_packets, st.tp_drops);

	printf("%d packets in ring (%d were dropped.)\n", sum, debug->drops);
}
#endif

__s32 sock_write (struct sctp_sock *ssock, struct arq_frame *buf, __u32 len)
{
	__s32 nwritten;
	int i = 0;

	if ((len < MIN_PACKET_SEND_SIZE) && (len != sizeof(struct arq_ackframe))) {
		len = MIN_PACKET_SEND_SIZE;
	}

	/* TODO: use PACKET_TX_RING (2.6.31)
	 *       - implement sendfile/splice in sending code => zero-copy sending */

	errno = 0;
	do {
		nwritten = write (ssock->sd, buf, len);
		i++;
	} while (errno == EAGAIN);

	if (i > 1)
		printf ("%d times buffer full on write\n", i);
	if (nwritten == -1) {
		perror ("Could not write to socket");
		assert (nwritten != -1);
	}

	if (nwritten < ((__s32) len))
		return SC_ABORT;
	return nwritten;
}

__s32 sock_writev (struct sctp_sock *ssock, const struct iovec *iov, int iovcnt)
{
	__s32 nwritten, len = 0;
	int i;

	for (i = 0; i < iovcnt; i++) {
		len += iov[i].iov_len;
	}

	i = 0;
	errno = 0;
	do {
		nwritten = writev (ssock->sd, iov, iovcnt);
		i++;
	} while (errno == EAGAIN);
	if (i > 1)
		printf ("%d times buffer full on write\n", i);
	assert (nwritten != -1);
	if (nwritten <  len)
		return SC_ABORT;
	return nwritten;
}


#ifdef DEBUG
__s32 debug_write (struct sctp_sock *ssock, struct arq_frame *buf, __u32 len)
{
	struct arq_frame* sf = (struct arq_frame*) buf->PAYLOAD;
	char* s;
	size_t sl, i, sll;
	char logstring[] = "Type: %2x Count: %3d Nathan: %04hx Payload: ";
	sl = sizeof(logstring) + 2 + 3 - 3 - 3 + 4 - 5;
	s = malloc (sl);
	snprintf  (s, sl, logstring, sf->SFLAGS, sf->NB, sf->NAT);
	write (ssock->debug_fd, s, sl-1);
	free(s);

	if (sf->SFLAGS == 0)      sl = sf->NB * sizeof(struct sctrl_cmd);
	else if (sf->SFLAGS >= 1) sl = sf->NB * sizeof(struct sctrl_cfg);

	s = malloc(256);
	memset(s, '\0', 256);
	for (i = 0; i < sl; i++) {
		sll = sprintf (s, "%02hhx ", sf->COMMANDS[i]);
		write (ssock->debug_fd, s, sll);
		assert(buf+14+len > sf->COMMANDS + i);
	}
	free(s);

	write(ssock->debug_fd, "\n", 1);
	return 0;
}
#endif
