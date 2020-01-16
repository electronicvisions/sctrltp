/*Implementation of the SCTP Userspace core
 *Compile with -lpthread and -lrt*/

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/time.h>

#include "sctrltp/us_sctp_core.h"

#define HOSTARQ_RESET_WAIT_SLEEP_INTERVAL 1000 /*in us*/

/*TODO: multiple instances (list!)*/
static struct sctp_core *admin = NULL;

// fwd decl
static void do_reset (bool fpga_reset);

double shitmytime() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return 1.0 * now.tv_sec + now.tv_usec / 1e6;
}


/** Try to unlink shared memory file.
 *  @return true if removal worked out, false otherwise.
 */
static bool try_unlink_shmfile (const char *NAME)
{
	__s32 fd;
	__s32 ret;

	fd = shm_open(NAME, O_CREAT | O_RDWR, 0666);
	if (fd < 0) {
		LOG_ERROR("Cannot non-exclusively open shared memory file (NAME: %s)", NAME);
		return false;
	}

	ret = flock(fd, LOCK_EX | LOCK_NB);
	if (ret < 0) {
		LOG_ERROR("Shared memory file is locked by running process (NAME: %s)", NAME);
		close(fd);
		return false;
	}

	/* we are the only lock-holder of the shared memory file, we may delete it */
	ret = shm_unlink(NAME);
	if (ret < 0) {
		LOG_ERROR("Could not unlink shared memory file (NAME: %s)", NAME);
		return false;
	}

	/* to be sure (lock gone and stuff) */
	close(fd);

	return true;
}

static void *create_shared_mem (const char *NAME, __u32 size)
{
	void *ptr = NULL;
	__s32 fd;
	__s32 ret;
	int len;
	char *cmd;

	mode_t prev = umask(0000);

	fd = shm_open (NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
	if ((fd < 0) && (errno == EEXIST)) {
		/* try again after trying to delete the shm file */
		if (try_unlink_shmfile(NAME)) {
			fd = shm_open (NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
		} else {
			umask(prev);
			return NULL;
		}
	}
	umask(prev);

	if (fd < 0)
	{
		perror ("ERROR: Failed to create new shared mem object");
		printf ("Please check if %s exists in shared memory (e.g. /dev/shm/%s)\n", NAME, NAME);

		/* This is very un-portable, Linux 2.6 stuff */
		printf ("Trying to display processes accessing the corresponding shared memory segment...\n");
		len = sizeof(char) * (strlen("fuser -uv /dev/shm/") + strlen(NAME) + 1);
		cmd = (char*) malloc(len);
		snprintf (cmd, len, "fuser -uv /dev/shm/%s", NAME);
		system (cmd);
		free ((void*) cmd);

		return NULL;
	}

	/* if two daemons start up at the same time one might finally fail here, i.e.
	 * it's racy but does not trigger illegal behavior */
	ret = flock(fd, LOCK_SH);
	if (ret < 0) {
		LOG_ERROR("Could not get shared lock on shared memory file (NAME: %s)", NAME);
		close(fd);
		shm_unlink(NAME);
		return NULL;
	}

	ret = ftruncate (fd, size);
	if (ret < 0)
	{
		LOG_ERROR("Failed to allocate mem for shared mem object (NAME: %s)", NAME);
		close (fd);
		ret = shm_unlink (NAME);
		return NULL;
	}
	ptr = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);
	if (ptr == MAP_FAILED)
	{
		/* retry without mem locking */
		ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (ptr == MAP_FAILED) {
			/* total fail */
			LOG_ERROR("Could not map mem to process space (NAME: %s)", NAME);
			close(fd);
			ret = shm_unlink(NAME);
			return NULL;
		} else {
			/* continue with pageable shmem, but warn the user */
			LOG_WARN("Could not lock shared mem to process space "
			         "(maybe max memlock too small? NAME: %s)", NAME);
		}
	}
#ifdef __i386__
	if ((((__u32)ptr)%4) != 0) LOG_WARN("Address not int aligned");
#else
	if ((((__u64)ptr)%4) != 0) LOG_WARN("Address not int aligned");
#endif
	close(fd);
	return ptr;
}

static void deallocate (__u8 state)
{
	(void)state;
	LOG_DEBUG ("> deallocating sub structures (level %d)\n", state);
	/*No need to deallocate in Userspace*/
}

static void mw_push_frames (struct sctp_fifo *fifo, struct sctp_alloc *local_buf, struct arq_frame *ptr, __u8 flush) {
	__u32 i;
	if (!ptr) {
		if (flush && ((i = local_buf->next) > 0)) {
			/*We dont have another frame, but want to flush remaining frames*/
			local_buf->num = i;
			local_buf->next = 0;
			fif_push (fifo, (__u8 *)local_buf, admin->inter);
			return;
		}
		return;
	} else {
		if ((i = local_buf->next) < PARALLEL_FRAMES) {
			/*There is room in local_buf to check frame in*/
			local_buf->fptr[i] = get_rel_ptr (admin->inter, ptr);
			local_buf->next++;
			if (flush) {
				/*Even if we have not fully filled local_buf, we want to push it ...*/
				local_buf->num = i + 1;
				local_buf->next = 0;
				fif_push (fifo, (__u8 *)local_buf, admin->inter);
			}
			return;
		} else {
			/*Local buf totally full, so lets push it up first ...*/
			local_buf->num = PARALLEL_FRAMES;
			local_buf->next = 0;
			fif_push (fifo, (__u8 *)local_buf, admin->inter);
			/*... but do not forget to register our frame*/
			local_buf->next = 1;
			local_buf->fptr[0] = get_rel_ptr (admin->inter, ptr);
			return;
		}
	}
}

static void push_frames (struct sctp_fifo *fifo, struct sctp_alloc *local_buf, struct arq_frame *ptr, __u8 flush) {
	__u32 i;
	if (!ptr) {
		if (flush && ((i = local_buf->next) > 0)) {
			/*We dont have another frame, but want to flush remaining frames*/
			local_buf->num = i;
			local_buf->next = 0;
			fif_push (fifo, (__u8 *)local_buf, admin->inter);
			return;
		}
		return;
	} else {
		if ((i = local_buf->next) < PARALLEL_FRAMES) {
			/*There is room in local_buf to check frame in*/
			local_buf->fptr[i] = get_rel_ptr (admin->inter, ptr);
			local_buf->next++;
			if (flush) {
				/*Even if we have not fully filled local_buf, we want to push it ...*/
				local_buf->num = i + 1;
				local_buf->next = 0;
				fif_push (fifo, (__u8 *)local_buf, admin->inter);
			}
			return;
		} else {
			/*Local buf totally full, so lets push it up first ...*/
			local_buf->num = PARALLEL_FRAMES;
			local_buf->next = 0;
			fif_push (fifo, (__u8 *)local_buf, admin->inter);
			/*... but do not forget to register our frame*/
			local_buf->next = 1;
			local_buf->fptr[0] = get_rel_ptr (admin->inter, ptr);
			return;
		}
	}
}

static __s32 do_startup (bool fpga_reset){
	do_reset(fpga_reset);
	return 0;
}

/* reset ARQ
 * \param reset if != 0, sends reset frame to remote partner
 */

static void do_hard_exit() {
	/* ignore aborting/failing threads */
	SCTP_CoreDown();
	/* signal the user-facing software */
	kill(getppid(), SIGHUP);
	exit(EXIT_FAILURE);
}

/*timeout for fpga +reset response*/
static void *fpga_reset_timeout() {
	LOG_INFO("Start new reset timer (NAME: %s)", admin->NAME);
	usleep(RESET_TIMEOUT);
	if (admin->STATUS.empty[0] == STAT_NORMAL) {
		pthread_exit(NULL);
	} else {
		LOG_ERROR("No reset response from FPGA (NAME: %s)", admin->NAME);
		do_hard_exit();
	}
	return NULL;
}

static void do_reset (bool fpga_reset) {
	__s32 b;
	struct sctp_alloc tmp1,tmp2;
	struct sctp_interface *inter = admin->inter;
	struct sctp_alloc *ptr;
	__u32 queue;
	__u32 offset;
	struct arq_resetframe resetframe;
	struct sockaddr_in reset_addr;
	memset (&tmp1, 0, sizeof (struct sctp_alloc));
	memset (&tmp2, 0, sizeof (struct sctp_alloc));
	pthread_t timer;

	/*Signal to threads not to do anything while reset in progress*/
	xchg (&(admin->STATUS.empty[0]), STAT_RESET);

	/*Acquire window locks*/
	spin_lock (&(admin->txwin.lock.lock));
	spin_lock (&(admin->rxwin.lock.lock));

	/*Recycle packet pointer saved in window entries to avoid memory leakage*/
	b = 0;
	while (b < MAX_NRFRAMES) {
		if (admin->txwin.frames[b].req) {
			memset (admin->txwin.frames[b].req, 0, sizeof (struct arq_frame));
			mw_push_frames (&(inter->alloctx), &tmp1, admin->txwin.frames[b].req, 0);
		}
		if (admin->rxwin.frames[b].resp) {
			memset (admin->rxwin.frames[b].resp, 0, sizeof (struct arq_frame));
			mw_push_frames (&(inter->allocrx), &tmp2, admin->rxwin.frames[b].resp, 0);
		}
		b++;
	}

	/*We have to be sure, everythings in alloc before returning (flush buffer)*/
	mw_push_frames (&(inter->alloctx), &tmp1, NULL, 1);
	mw_push_frames (&(inter->allocrx), &tmp2, NULL, 1);

	/*set some vars to initial values again*/
	admin->ACK = MAX_NRFRAMES-1;
	admin->REQ = 0;
	admin->rACK = admin->ACK;

	/*Reset windows (txwin, rxwin)*/
	win_reset (&(admin->rxwin));
	win_reset (&(admin->txwin));

	/*Release window locks*/
	spin_unlock (&(admin->rxwin.lock.lock));
	spin_unlock (&(admin->txwin.lock.lock));

	/*Cycle through queues*/
	for (queue = 0; queue < NUM_QUEUES; queue++) {
		/*Reset tx and rx queues*/
		spin_lock (&(inter->tx_queues[queue].nr_full.lock));
		spin_lock (&(inter->rx_queues[queue].nr_full.lock));

		if ((b = inter->tx_queues[queue].nr_full.semval) > 0) {
			/*There are buffers in tx_queue*/
			/*Get pointer to buffer in shared mem*/
			ptr = (struct sctp_alloc *)get_abs_ptr(inter, inter->tx_queues[queue].buf);
			/*Calculate offset of non-empty buffers*/
			offset = (inter->tx_queues[queue].last_out + 1) % inter->tx_queues[queue].nr_elem;
			while (b > 0) {
				fif_push (&(inter->alloctx), (__u8 *)(ptr+offset+b), inter);
				b--;
			}
		}
		if ((b = inter->rx_queues[queue].nr_full.semval) > 0) {
			/*There are buffers in rx_queue*/
			/*Get pointer to buffer in shared mem*/
			ptr = (struct sctp_alloc *)get_abs_ptr(inter, inter->rx_queues[queue].buf);
			/*Calculate offset of non-empty buffers*/
			offset = (inter->rx_queues[queue].last_out + 1) % inter->rx_queues[queue].nr_elem;
			while (b > 0) {
				fif_push (&(inter->allocrx), (__u8 *)(ptr+offset+b), inter);
				b--;
			}
		}

		fif_reset (&(inter->tx_queues[queue]));
		fif_reset (&(inter->rx_queues[queue]));

		spin_unlock (&(inter->tx_queues[queue].nr_full.lock));
		spin_unlock (&(inter->rx_queues[queue].nr_full.lock));

		/*TODO: Maybe we have to wake someone here, but that is left open till signal mechanism is fully supported*/
	}

	/*Send reset frame to remote host, if we have to*/
	if (fpga_reset) {
		sctpreset_init(&resetframe);

		memset(&reset_addr, 0, sizeof(reset_addr));
		reset_addr.sin_family = AF_INET;
		reset_addr.sin_port = htons(UDP_RESET_PORT); /* reset source port => sets FPGA target port */
		reset_addr.sin_addr.s_addr = admin->sock.remote_ip;
		b = sendto(admin->sock.sd, &resetframe, sizeof(struct arq_resetframe), /*flags*/ 0,
		           (struct sockaddr *)&reset_addr, sizeof(reset_addr));
		if (b <= 0) {
			LOG_ERROR("Could not send reset frame (write to socket failed for NAME: %s); error %s", admin->NAME, strerror(errno));
			pthread_exit(NULL);
		} else {
			LOG_INFO("Sent reset frame. Waiting for response... (NAME: %s)", admin->NAME);
		}
		/*Signal thread should wait for reset respones*/
		xchg (&(admin->STATUS.empty[0]), STAT_WAITRESET);
		pthread_create(&timer, NULL, fpga_reset_timeout , NULL);
	} else {
		/*Signal threads should work normally*/
		xchg (&(admin->STATUS.empty[0]), STAT_NORMAL);
	}
}

void *SCTP_PREALLOC (void *core)
{
	struct sctp_core *ad = core;
	__u32 i;
	__u32 num;
	struct sctp_alloc tmp;
	struct arq_frame *buf = ad->inter->pool;

	LOG_INFO ("PREALLOC STARTED PREALLOCATION (LOWADDR: %p)", (void *)buf);
	memset (&tmp, 0, sizeof (struct sctp_alloc));

	i = 0;
	num = 0;
	/*Fill alloc fifo with empty elements*/
	while (i < ALLOCTX_BUFSIZE) {

		memset (buf, 0, sizeof(struct arq_frame));

		/*Push to shared fifo with passing baseptr to recalculate absolute pointer to entry*/
		mw_push_frames (&(ad->inter->alloctx), &tmp, buf, 0);

		buf++;
		i++;
		num++;
	}
	memset (&tmp, 0, sizeof (struct sctp_alloc));

	i = 0;
	/*Fill alloc fifo with empty elements*/
	while (i < ALLOCRX_BUFSIZE) {

		memset (buf, 0, sizeof(struct arq_frame));

		/*Push to shared fifo with passing baseptr to recalculate absolute pointer to entry*/
		mw_push_frames (&(ad->inter->allocrx), &tmp, buf, 0);

		buf++;
		i++;
		num++;
	}


	LOG_INFO ("PREALLOC SUCCESSFULLY EXITS (# %u/%lu)", num, (unsigned long int) (ALLOCTX_BUFSIZE+ALLOCRX_BUFSIZE));
	LOG_INFO ("HIGHADDR: %p", (void *)buf);
	pthread_exit(NULL);
}

/*Rx thread*/
void *SCTP_RX (void *core)
{
	__s32 a;
	__s32 b;
	__s32 nread;
	__u32 i, j;

	/*Fallback buffer*/
	struct arq_frame local_buf;
	__u8 local = 0;

	/*Shortcuts to used elements*/
	struct sctp_core *ad = core;
	struct sctp_interface *inter = ad->inter;

	/*Local cache*/
	struct sctp_alloc in;
	struct sctp_fifo *outfifo = NULL;
	struct sctp_alloc out[NUM_QUEUES];
	__u16 queue = 0;
	struct arq_frame *curr_packet = NULL;
	struct sctp_internal outbuf_rx[MAX_WINSIZ];
	struct sctp_fifo *infifo = &(ad->inter->allocrx);
	struct sctp_sock *sock = &(ad->sock);
	struct sctp_stats *stats = &(ad->inter->stats);
	struct sctp_window *outwin = &(ad->rxwin);
	struct semaphore *sig = &(ad->inter->waketx);

	__s32 seq;
	__u32 size;
	__u32 rack;
	__u32 rack_old;
	__u64 data, acktime = 0;
#ifdef _SCTP_HWPOLICY
#error "deprecated!! Leads to erroneous behaviour on HW"
	__u32 nrpackrcvd = 0;
#endif

	if (prctl (PR_SET_NAME, "RX", NULL, NULL, NULL))
		printf("Setting process name isn't supported on this system.\n");

	memset (outbuf_rx, 0, sizeof(struct sctp_internal)*MAX_WINSIZ);
	memset (&in, 0, sizeof (struct sctp_alloc));
	memset (out, 0, sizeof (struct sctp_alloc)*NUM_QUEUES);

	ad->ACK = (MAX_NRFRAMES-1);
	ad->rACK = (MAX_NRFRAMES-1);
	rack = (MAX_NRFRAMES-1);
	rack_old = rack;
#ifndef WITH_ROUTING
	/*without multiple queue support, there is only one*/
	outfifo = &(inter->rx_queues[0]);
#endif

	LOG_INFO("RX UP");

	/*MAIN LOOP*/
	while (1) {
		/*Fetch pointer to empty space to receive a packet*/
		if ((!curr_packet) || (local == 1)) {
			local = 0;
			if ((i = in.next) < in.num) {
				/*We have buffers in cache*/
				in.next++;
				curr_packet = in.fptr[i];
			} else {
				/*We dont have empty buffers in cache, so lets fetch new ones*/
				b = try_fif_pop (infifo, (__u8 *)&in, inter);
				if (b != SC_EMPTY) {
					/*Yippey, we got frames to handle!*/
					for (i = 0; i < in.num; i++) {
						in.fptr[i] = get_abs_ptr (inter, in.fptr[i]);
					}
					in.next = 1;
					curr_packet = in.fptr[0];
				} else {
					/*allocrx is empty, so we have to fall back on local buffer*/
					local = 1;
					curr_packet = &local_buf;
				}
			}
		}

		if (local == 0) assert (curr_packet >= inter->pool);

		/*Read packet from socket*/
		nread = sock_read (sock, curr_packet, 1);
		if (nread == SC_ABORT) {
			LOG_ERROR("Read from socket failed! Aborting RX thread... (NAME: %s)", admin->NAME);
			pthread_exit (NULL);
		}
		stats->nr_received++;
		while (nread == SC_INVAL) {
			stats->nr_protofault++;
			nread = sock_read (sock, curr_packet, 1);
			if (nread == SC_ABORT) {
				LOG_ERROR("Read from socket failed! Aborting RX thread... (NAME: %s)", admin->NAME);
				pthread_exit (NULL);
			}
			stats->nr_received++;
		}

		size = sctpsomething_get_size(curr_packet, nread);
		if ((__u32)nread != size) {
			LOG_WARN("Received bytes on-wire and sctp.size do not match: %d != %d (dropping!) "
				     "(NAME: %s)", nread, size, admin->NAME);
			continue;
		}

		/*Check if waiting for fpga reset response*/
		if (unlikely(ad->STATUS.empty[0] == STAT_WAITRESET)) {
			/*Checking if recived packet is config packet*/
			if(sctpreq_get_typ(curr_packet) == PTYPE_CFG_TYPE) {
				/*recieved config packet, setting threads to normal*/
				xchg (&(ad->STATUS.empty[0]), STAT_NORMAL);
			}
			/*received packet was not a config packet, check timeout*/
			else{
				continue; // drop all other packets
			}
		}

		/*Check if we can operate normally*/
		if (likely(ad->STATUS.empty[0] == STAT_NORMAL)) {
					/*Determine attributes of packet*/
					rack = sctpreq_get_ack (curr_packet);

					/*Is ACK a new remote ACK received?*/
					if (rack != rack_old) {
						/*Pass new ACK to TX and wake him up*/
						rack_old = rack;
						//printf("new rack: %d\n", rack);

						xchg ((__s32 *)&(ad->rACK), (__s32)rack);

						cond_signal (sig, 1, 1);
					}
					seq = -1;
					if (size > sizeof(struct arq_ackframe))
						seq = sctpreq_get_seq (curr_packet);

					queue = 0;

					/*First check if seq valid and there is room in buffer ... if not, drop it! do NOT insert local_buf!!*/
					if ((seq >= 0) && (outfifo->nr_full.semval <= (__s32)(outfifo->nr_elem - MAX_WINSIZ)) && (local == 0)) {
						b = new_frame_rx(outwin, curr_packet, outbuf_rx);
						a = 0;
						if (b > 0) {
							/*There are new frames in order from remote to pass back to user*/
							while (a < b) {
								/* Check if Packet is reset answer from FPGA*/
								if (unlikely(sctpreq_get_typ(curr_packet) == PTYPE_CFG_TYPE)) {
									/* Fromating variables */
									LOG_INFO("Got reset answer, dropping data (NAME: %s)", admin->NAME);
									const char *resetframe_var_names[] = {"MAX_NRFRAMES\t","MAX_WINSIZ\t","MAX_PDUWORDS\t"};
									bool wrong_hw_settings = false;

									for (j = 0; j < sctpreq_get_len(curr_packet); j++){
										data = be64toh(sctpreq_get_pload(curr_packet)[j]);
										if (data == resetframe_var_values_check[j])
											LOG_INFO("\t%s\t%llu\t OK!", resetframe_var_names[j], data);
										else {
											LOG_ERROR("\t%s\t Sent by FPGA: %llu\t Expected by Host: %lu (NAME: %s)",
												resetframe_var_names[j], data, resetframe_var_values_check[j],
												admin->NAME);
											wrong_hw_settings = true;
										}
									}
									if (wrong_hw_settings) {
										fprintf (stderr, "ERROR: Mismatch of software and FPGA hardware settings, maybe old or experimental FPGA Bitfile\n"\
												"If you are sure that the bitfile is correct change values in sctrltp/userspace/packets.h\n"\
												"if not please contact a FPGA person of your choice (Vitali Karasenko, Christian Mauch, Eric Mueller)\n");
										do_hard_exit();
									}
									/*drop the answer packet*/
									a++;
									break;
								}

								/*Pass packet to upper layer*/
								if ((i = out[queue].next) < PARALLEL_FRAMES) {
									/*There is room in local_buf to check frame in*/
									out[queue].fptr[i] = get_rel_ptr (inter, outbuf_rx[a].resp);
									out[queue].next++;
								} else {
									/*Local buf totally full, so lets push it up first ...*/
									out[queue].num = PARALLEL_FRAMES;
									out[queue].next = 0;
									fif_push (outfifo, (__u8 *)&out[queue], inter);
									/*... but do not forget to register our frame*/
									out[queue].next = 1;
									out[queue].fptr[0] = get_rel_ptr (inter, outbuf_rx[a].resp);
								}

								a++;
							}

							for (queue = 0; queue < NUM_QUEUES; queue++) {
								outfifo = &(inter->rx_queues[queue]);
								/*Flush any remaining frames*/
								if ((i = out[queue].next) > 0) {
									/*We dont have another frame, but want to flush remaining frames*/
									out[queue].num = i;
									out[queue].next = 0;
									fif_push (outfifo, (__u8 *)&out[queue], inter);
								}
							}

							/*Update ACK field if window was slided*/
							xchg ((__s32 *)&(ad->ACK), (__s32)((outwin->low_seq - 1) % outwin->max_frames));
						}

						/*TODO: Maybe implement a different strategy if HW supports this*/
						/*Acknowledge everything (AE Strategy)*/
						/*Delayed acknowledgement strategy (every 100 ms)*/
						if (ad->currtime >= (acktime + DELAY_ACK)) {
							ad->REQ = 1;
							cond_signal (sig, 1, 1);
							acktime = ad->currtime;
						}

						/*Frame was registered successfully, so it is passed to upper layer*/
						if (b >= 0) {
							stats->bytes_recv_payload += sctpreq_get_size(curr_packet);
							stats->nr_received_payload++;
							curr_packet = NULL;
						} else {
							/*sequence was valid but out of window*/
							stats->bytes_recv_oow += sctpreq_get_size(curr_packet);
							stats->nr_outofwin++;
						}
					} else {
						/*Congested case: We have to drop this frame :(*/
						if (seq >= 0) stats->nr_congdrop++;
					}
		} else stats->nr_congdrop++;
		/*Nothing more to do, so may fetch another buffer*/
	}
	pthread_exit(NULL);
}

/*Tx thread*/
void *SCTP_TX (void *core)
{
	__s32 b;
	__s32 a = 0;
	struct sctp_core *ad = core;
	struct sctp_interface *inter = ad->inter;
	struct sctp_fifo *infifo = ad->inter->tx_queues;
	struct sctp_alloc in[NUM_QUEUES];
	__u16 queue = 0;
	__u16 cycle = 0;
	struct sctp_alloc out;
	struct arq_frame *curr_packet = NULL;
	struct sctp_fifo *outfifo = &(ad->inter->alloctx);
	struct sctp_window *outwin = &(ad->txwin);
	__vs32 *wlock = &(ad->txwin.lock.lock);
	struct sctp_stats *stats = &(ad->inter->stats);
	struct sctp_sock *sock = &(ad->sock);
	struct semaphore *sig = &(ad->inter->waketx);
	struct sctp_internal outbuf_tx[MAX_WINSIZ];

	struct arq_ackframe ackpacket;

	__u32 size;
	__u32 acksize;
	__u32 i;

	__u32 curr_rack = MAX_NRFRAMES-1;
	__u32 old_rack = MAX_NRFRAMES-1;

#ifdef WITH_RTTADJ
	__s64 mRTT;
	__s64 err;
	__s64 avg = MAX_RTO;
	__s64 dev = TO_RES;
	__s64 res;
#endif

	if (prctl (PR_SET_NAME, "TX", NULL, NULL, NULL))
		printf("Setting process name isn't supported on this system.\n");

	memset (in, 0, sizeof (struct sctp_alloc) * NUM_QUEUES);
	memset (&out, 0, sizeof (struct sctp_alloc));
	memset (&ackpacket, 0, sizeof (struct arq_ackframe));
	acksize = sizeof(struct arq_ackframe);

	LOG_INFO("TX UP");

	/*MAIN LOOP*/
	while (1) {
		/*Check, if we are allowed to operate normally*/
		if (likely(ad->STATUS.empty[0] == STAT_NORMAL)) {
			/*Try to fetch Paket from queues, if old one was processed before*/
			cycle = 0;
			/*Loop through tx_queues till frames found or one cycle passed unsuccessfully*/
			while ((curr_packet == NULL) && (cycle < NUM_QUEUES)) {
				infifo = &(inter->tx_queues[queue]);
				if ((i = in[queue].next) < in[queue].num) {
					/*We have a frame in local cache*/
					in[queue].next++;
					curr_packet = in[queue].fptr[i];
				} else {
					/*We dont have an unprocessed frame, so lets fetch new ones*/
					b = try_fif_pop (infifo, (__u8 *)&in[queue], inter);
					if (b != SC_EMPTY) {
						/*Yippey, we got frames to handle!*/
						for (i = 0; i < in[queue].num; i++) {
							in[queue].fptr[i] = get_abs_ptr (inter, in[queue].fptr[i]);
						}
						in[queue].next = 1;
						curr_packet = in[queue].fptr[0];
					} else {
						/*Update queue index and cycle counter*/
						queue = (queue + 1) % NUM_QUEUES;
					}
				}

				cycle++;
			}

			/*Update values from RX*/
			curr_rack = ad->rACK;

			if (curr_packet == NULL) {
				spin_lock (wlock);
				/*Check, if we can slide our window*/
				if (curr_rack != old_rack) {
					a = mark_frame (outwin, curr_rack, outbuf_tx);
					old_rack = curr_rack;
				}

				/*We do not have a packet for transmission, so we check on a possible ACK transmission*/
				if (ad->REQ) {
					/*Indeed, we set up an ACK frame and transmit it*/
					sctpack_set_ack (&ackpacket, ad->ACK);
					ad->REQ = 0;
					b = sock_write (sock, (struct arq_frame *)&ackpacket, acksize);
					if (b<0) {
						LOG_ERROR("Could not send ack (write to socket failed for NAME: %s)", admin->NAME);
						pthread_exit(NULL);
					}
				}
				spin_unlock (wlock);

				if (a <= 0) {
					/*There is really nothing to do for us, so we wait :)*/
					cond_wait (sig, 1);
				}
			} else {
				/*Handle Packet by type*/
//				TODO check for: sctpreq_get_pload(curr_packet)[0] == htobe64(HW_HOSTARQ_MAGICWORD)) {
						spin_lock (wlock);
						/* Check for ARQ reset command */
						if (sctpreq_get_typ(curr_packet) == PTYPE_DO_ARQRESET && sctpreq_get_pload(curr_packet)[0] == htobe64(HW_HOSTARQ_MAGICWORD)) {
							LOG_INFO("Got reset command, resetting FPGA (NAME: %s)...", admin->NAME);
							curr_packet = NULL;
							spin_unlock (wlock);
							do_reset(true);
							continue;
						}

						/*Check, if we can slide our window*/
						if (curr_rack != old_rack) {
							a = mark_frame (outwin, curr_rack, outbuf_tx);
							old_rack = curr_rack;
						}

						/*Try to put new frame into window*/
						b = new_frame_tx (outwin, curr_packet, ad->currtime);

						if (unlikely(b == SC_ABORT)) {
							LOG_ERROR("Could not register frame in window (NAME: %s)", admin->NAME);
							pthread_exit(NULL);
						}

						if (b > 0) {
							/*Frame was registered in window, lets send it*/
							size = sctpreq_get_size(curr_packet);
							/* Send Frame with ACK and delete signal if necessary
							 * to suppress transmission of ACK frames*/
							sctpreq_set_ack (curr_packet, ad->ACK);
							ad->REQ = 0;
							b = sock_write (sock, curr_packet, size);
							assert(b % 4 == 0); // assert on alignment
							spin_unlock (wlock);
#ifdef DEBUG
							debug_write (sock, curr_packet, size);
#endif
							if (b<0) {
								LOG_ERROR("Could not write data to socket (NAME: %s)", admin->NAME);
								pthread_exit(NULL);
							}

							/*Updating statistics (bytes_sent)*/
							stats->bytes_sent += size;
							stats->bytes_sent_payload += size;
							curr_packet = NULL;
						} else {
							// FIXME: will be done in next iteration anyway?
							/*Frame wasnt registered in window, so may transmit an ACK frame*/
							if (ad->REQ) {
								/*Indeed, we set up an ACK frame and transmit it*/
								sctpack_set_ack (&ackpacket, ad->ACK);
								ad->REQ = 0;
								b = sock_write (sock, (struct arq_frame *)&ackpacket, acksize);
								if (b<0) {
									LOG_ERROR("Could not send ack (write to socket failed for NAME: %s)", admin->NAME);
									pthread_exit(NULL);
								}
							}
							spin_unlock (wlock);
							if (a <= 0) {
								/*There is really nothing to do for us, so we wait :)*/
								cond_wait (sig, 1);
								sched_yield();
							}
						}
			}

#ifdef WITH_RTTADJ
#ifdef WITH_CONGAV
			/* don't update rtt if congestion occurs... it will rise to MAX otherwise */
			if (a > 0 && !outwin->flag) {
#else
			if (a > 0) {
#endif // WITH_CONGAV
				/*Measure difference between last transmitted and already acked packet and current time*/
				mRTT = ad->currtime - outbuf_tx[a-1].time;

				/*Adjusting round trip time with measured one*/
				err = (mRTT - avg);
				avg += (err / 8);
				err = llabs(err);

				dev += ((err - dev) / 4);
				if (dev < TO_RES)
					dev = TO_RES;
				res = avg + 4*dev;
				if (res < MIN_RTO)
					res = MIN_RTO;
				if (res > MAX_RTO)
					res = MAX_RTO;

				/*Put new value into statistics field*/
				stats->RTT = (__u64)res;
			}
#endif

			/*Push checked out frames to alloc queue*/
			while (a > 0) {
				push_frames (outfifo, &out, outbuf_tx[a-1].req, 0);
				a--;
			}
		}
		/*Nothing to do here anymore, so lets fetch another packet*/
	}
	pthread_exit(NULL);
}

/* This thread periodically checks if there is an old packet, which has to be resend
 * TODO: Maybe set up another rtc/hpet timer for this thread*/
void *SCTP_RESEND (void *core)
{
	struct sctp_core *ad = core;
	struct sctp_window *txwin = &(ad->txwin);
	struct sctp_stats *stats = &(ad->inter->stats);
	__vs32 *wlock = &(ad->txwin.lock.lock);
	struct sctp_sock *sock = &(ad->sock);
	struct sctp_internal resend[MAX_NRFRAMES];
	struct arq_frame *packet;
	__s32 ret;
	__u32 size;
	__u32 a;
	__s32 b;
	__u64 time2wait = MAX_RTO;
#ifndef WITH_HPET
	struct timespec towait;
	struct timespec remain;
	towait.tv_sec = 0;
	towait.tv_nsec = TO_RES * 1000;
#endif

	if (prctl (PR_SET_NAME, "RETRANSMIT", NULL, NULL, NULL))
		printf("Setting process name isn't supported on this system.\n");

	ad->inter->stats.RTT = MAX_RTO;

	LOG_INFO ("RESEND UP");

	/*MAIN LOOP*/
	/*NOTE: Check state here (if Core is up etc)*/
	while (1) {
		/*Sleep a defined time*/
#ifndef WITH_HPET
		nanosleep(&towait, &remain);
#else
		timer_poll (&(ad->txtimer));
#endif

		/*Update current time*/
		ad->currtime += TO_RES;

		/*Update remaining wait time*/
		if (time2wait > TO_RES)
			time2wait -= TO_RES;
		else
			time2wait = 0;

		if (time2wait > 0) {
			sched_yield();
			continue; /* still have to wait some time ... */
		} else {
			time2wait = ad->inter->stats.RTT;
		}

		/*Check if we can operate normally*/
		if (likely(ad->STATUS.empty[0] == STAT_NORMAL)) {
			if (spin_try_lock (wlock)) {
				/*Try to resend oldest frames*/
				if ((ret = resend_frame (txwin, resend, stats->RTT, ad->currtime)) > 0)
				{
					a = 0;
					while (a < (__u32)ret) {
						packet = resend[a].req;

						size = sctpreq_get_size(packet);

						/* Send old packet and merge it with ACK published from RX recently*/
						sctpreq_set_ack (packet, ad->ACK);
						ad->REQ = 0;
						b = sock_write (sock, packet, size);
						assert(b % 4 == 0); // assert on alignment
						if (b<0) {
							LOG_ERROR("Could not resend frame (write to socket failed for NAME: %s)", admin->NAME);
							pthread_exit(NULL);
						}

						/*Updating statistics*/
						stats->bytes_sent_resend += size;
						stats->bytes_sent += size;
						a++;
					}
				}
				if (ret == -1)
					//resend timeout
					do_hard_exit();
				spin_unlock (wlock);
			}
		}
		/*Lets sleep again*/
	}
#ifdef WITH_HPET
	timer_close (&(ad->txtimer));
#endif
	pthread_exit(NULL);
}

/*Used by start_core.c to get pointer to local core structure*/
struct sctp_core *SCTP_debugcore (void)
{
	if (admin) return admin;
	return NULL;
}


/*This function prepares and start SCTP algorithm then returning a descriptor (on error returning a negative value)
*/
__s8 SCTP_CoreUp (char const *name, char const *rip, __s8 wstartup)
{
	__s32 c;
	__u32 k;
	__s8 ret;
	struct sctp_interface *interface = NULL;
	__u16 i;
	struct sctp_alloc *txbuf_ptr = NULL;
	struct sctp_alloc *rxbuf_ptr = NULL;
	__u32 remote_ip;

#define TX_BUFSPQ (TX_BUFSIZE/NUM_QUEUES)
#define RX_BUFSPQ (RX_BUFSIZE/NUM_QUEUES)

	LOG_INFO ("SCTP CORE OPEN CALLED");

	if (!rip)
		return -1;

	if (admin)
		return 1;

	if (!admin)
		admin = malloc (sizeof(struct sctp_core));
	if (!admin) {
		return -5;
	} else {
		memset (admin, 0, sizeof(struct sctp_core));
	}
	admin->NAME = name;
	LOG_INFO("> main structure allocated successfully");

	/*Initialize structures (allocating buffers etc.)*/

	interface = (struct sctp_interface *) create_shared_mem ((char *)name, sizeof(struct sctp_interface));
	if (!interface) {
		deallocate(1);
		return -5;
	}

	memset (interface, 0, sizeof(struct sctp_interface));

	admin->inter = interface;
	LOG_INFO ("BASEADDR: %p POOLADDR: %p", (void *)admin->inter, (void *)admin->inter->pool);

	remote_ip = inet_addr(rip);

	/*Init conditional variable used by TX thread*/
	cond_init (&(admin->inter->waketx));
	admin->inter->waketx.semval = 0;

	/*First initialising windows*/
	ret = win_init(&(admin->txwin), MAX_NRFRAMES, MAX_WINSIZ, SCTP_TXWIN);
	if (ret < 0) {
		deallocate(1);
		return -5;
	}
	ret = win_init(&(admin->rxwin), MAX_NRFRAMES, MAX_WINSIZ, SCTP_RXWIN);
	if (ret < 0) {
		deallocate(2);
		return -5;
	}

	/*Initializing fifos*/
#ifdef WITH_ROUTING
	LOG_INFO ("> Routing capability enabled. Initializing multiqueues...");
#endif
	txbuf_ptr = interface->txq_buf;
	rxbuf_ptr = interface->rxq_buf;
	for (i = 0; i < NUM_QUEUES; i++) {
		LOG_INFO ("> Init queue pair %u. Buffers per queue: %u TX %u RX", i, TX_BUFSPQ, RX_BUFSPQ);
		ret = fif_init_wbuf(&(interface->tx_queues[i]),TX_BUFSPQ,sizeof(struct sctp_alloc),(__u8*)txbuf_ptr,interface);
		if (ret < 0) {
			deallocate(3);
			return -5;
		}

		ret = fif_init_wbuf(&(interface->rx_queues[i]),RX_BUFSPQ,sizeof(struct sctp_alloc),(__u8*)rxbuf_ptr,interface);
		if (ret < 0) {
			deallocate(4);
			return -5;
		}
		txbuf_ptr += TX_BUFSPQ;
		rxbuf_ptr += RX_BUFSPQ;
	}

	/*We allocate fifos with enough empty frames to avoid performance loss (see PREALLOCATE)*/
	ret = fif_init_wbuf(&(admin->inter->alloctx),ALLOCTX_BUFSIZE,sizeof(struct sctp_alloc),(__u8*)admin->inter->alloctx_buf,admin->inter);
	if (ret < 0) {
		deallocate(7);
		return -5;
	}

	ret = fif_init_wbuf(&(admin->inter->allocrx),ALLOCRX_BUFSIZE,sizeof(struct sctp_alloc),(__u8*)admin->inter->allocrx_buf,admin->inter);
	if (ret < 0) {
		deallocate(7);
		return -5;
	}

	LOG_INFO ("> sub structures successfully initialized");

	/*End of allocation, next we have to open the socket*/
	c = sock_init (&(admin->sock), &remote_ip);
	if (c != 0) {
		deallocate(8);
		return -4;
	}

	LOG_INFO ("> socket opened and bound to device");

	/*If socket is up, we can finally start the threads*/

	LOG_INFO ("> initializing FPGA...");
	if (do_startup(wstartup) < 0) {
		deallocate(9);
		return -4;
	}
	LOG_INFO ("> backplane initialized");


	c = pthread_create (&admin->allocthr, NULL, SCTP_PREALLOC, admin);
	if (c != 0) {
		deallocate(9);
		return -4;
	}

	(void) pthread_join (admin->allocthr, NULL);

	c = pthread_create (&admin->rxthr, NULL, SCTP_RX, admin);
	if (c != 0) {
		deallocate(11);
		return -4;
	}

#ifdef WITH_HPET
	/* Before we start RESEND, we need to set up his timer */
	c = timer_init (&(admin->txtimer), "/dev/hpet", 1000000/TO_RES);
	if (c < 0) {
		deallocate(13);
		return -4;
	} else {
		printf ("RESEND timer started @ %dHz\n", 1000000/TO_RES);
	}
#endif

#ifdef WITH_CONGAV
	LOG_INFO ("Congestion avoidance active");
#endif

#ifdef WITH_RTTADJ
	LOG_INFO ("RTO adjust active");
#endif

	c = pthread_create (&admin->rsthr, NULL, SCTP_RESEND, admin);
	if (c != 0) {
		deallocate(12);
		return -4;
	}

	c = pthread_create (&admin->txthr, NULL, SCTP_TX, admin);
	if (c != 0) {
		deallocate(14);
		return -4;
	}

	LOG_INFO ("> Threads up");

	/* Check if FPGA reset was succsessful */
	for (k = 0; k < RESET_TIMEOUT; k += HOSTARQ_RESET_WAIT_SLEEP_INTERVAL) {
		if (admin->STATUS.empty[0] == STAT_NORMAL)
			break;
		usleep(HOSTARQ_RESET_WAIT_SLEEP_INTERVAL); // sleep a bit
	}

	if (k >= RESET_TIMEOUT) {
		deallocate(15);
		return -4;
	}

	LOG_INFO ("SCTP CORE OPEN SUCCESSFUL (max pdu words: %d, window size: %d, HW_DELAY_ACK: %d, DELAY_ACK: %d)", MAX_PDUWORDS, MAX_WINSIZ, HW_DELAY_ACK, DELAY_ACK);

	/*TODO: Update mem counter in stats*/

	return 1;
}

/*Stops algorithm, frees mem and gives statuscode back*/
__s8 SCTP_CoreDown (void /* as long there is only one single core pointer */)
{
	memfence();
	if (admin == NULL) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
		fprintf(stderr, "%s: cannot do anything, my memory is already gone\n", __FUNCTION__);
#pragma GCC diagnostic pop
		return 0;
	}

	/* TODO: add wait for workers (RX, TX, RESEND) but time out if takes too long */

	/* use admin pointer locally, but destroy global before finishing */
	struct sctp_core * my_admin = admin;
	LOG_INFO ("Shutting down SCTP core: %s (pid %u)", my_admin->NAME, getpid());

	admin = NULL;
	memfence();

	if (my_admin == NULL) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
		LOG_ERROR("Lost race, my memory is already gone");
#pragma GCC diagnostic pop
		return 0;
	}

	munmap (my_admin->inter, sizeof(struct sctp_interface));
	shm_unlink (my_admin->NAME);
	free (my_admin->txwin.frames);
	free (my_admin->rxwin.frames);
	free (my_admin);
	return 1;
	/*In Userspace we will do nothing here ... who cares, I care (ECM!) */

	/* ECM: TODO - This parts are missing */
	/*Deallocate everything*/
	/*TODO: Give instance free*/
}

#undef HOSTARQ_RESET_WAIT_SLEEP_INTERVAL
