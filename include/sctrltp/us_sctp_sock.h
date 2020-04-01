#pragma once

#include "sctrltp/build-config.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <linux/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/uio.h>
#include "us_sctp_defs.h"

#include "packets.h"

/* Berkeley Packet Filter */
#ifdef WITH_BPF
#include "bpf.h"
#endif

/* PACKET_RX_RING stuff */
#ifdef WITH_PACKET_MMAP
#include "packets.h"
#include <linux/if_packet.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/user.h>
/* should be true on 2.6/i386 kernel */
#define MAX_ORDER 11
#else
#include <netpacket/packet.h>
#endif

namespace sctrltp {

#ifdef WITH_PACKET_MMAP
static struct sctp_sock *debug;
#endif

struct sctp_sock {
	__s32 sd;
#ifdef DEBUG
	__s32 debug_fd;
#endif
#ifdef WITH_PACKET_MMAP
	#define KILOBYTE         1024
	#define MEGABYTE        (1024 * KILOBYTE)

	#define	TP_BLOCK_SIZE   (  16 * KILOBYTE)
	#define	TP_FRAME_SIZE   (  16 * KILOBYTE)
	/* size has to be 2<<X */
	#define	TP_BLOCK_NR     (   8 * MEGABYTE / TP_BLOCK_SIZE)
	#define	TP_FRAME_NR     (TP_BLOCK_SIZE / TP_FRAME_SIZE * TP_BLOCK_NR)
	void *ring_ptr;         /* pointer to rx ring                       */
	void **ring;            /* pointer to frame pointers                */
	int ring_cnt;
	int ring_idx;
	int drops;
	struct pollfd pfd;      /* poll structure for poll()-for-new-packet */
	struct tpacket_req req; /* rx ring request structure                */
#endif
	__u32 local_ip;
	__u32 remote_ip;
};

/*Initializes raw packet socket and bind it to the given device*/
__s8 sock_init (struct sctp_sock *ssock, const __u32 *rip);

/* returns number of bytes stored into buf (should be HEADER+NB*SCTRLCMD bytes)
 * filter = 1: filter enabled, will return -1 if frame has not passed filter checks*/
__s32 sock_read (struct sctp_sock *ssock, struct arq_frame *buf, __u8 filter);

/*returns number of bytes actually written (should be equal to len, if its not -4 is returned)*/
__s32 sock_write (struct sctp_sock *ssock, struct arq_frame *buf, __u32 len);

/*returns number of bytes actually written (should be equal to len, if its not -4 is returned)*/
__s32 sock_writev (struct sctp_sock *ssock, const struct iovec *iov, int iovcnt);

void print_stats ();

#ifdef DEBUG
__s32 debug_write(struct sctp_sock *ssock, struct arq_frame *buf, __u32 len);
#endif

} // namespace sctrltp
