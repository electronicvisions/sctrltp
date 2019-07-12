/*Program to start SCTP-Core safely*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/ioctl.h>

#include "sctrltp/start_core.h"
#include "sctrltp/us_sctp_core.h"
#include "sctrltp/packets.h"


static __s32 post_init = 0;
static __s32 exiting = 0;

void exit_handler (void);

int main (int argc, char **argv)
{
	__s8 retval;
	char *rip;
	__s8 init = 1;
	char blubb;

	printf ("SCTP Core startup program\n");
	if (argc < 3 || argc > 3) {
		printf ("Usage: %s <Remote IP> (<FPGA mode>)\n", argv[0]);
		printf ("Network interfaces found:\n");
		printInterfaces();
		return 1;
	}

	rip = argv[1];

	/* initialize backplane? (flag) */
	if (argc == 3)
		init = (__u8) atoi (argv[2]);

	/* activate signal (termination) handler but keep ignored signals */
	if (signal (SIGINT, termination_handler) == SIG_IGN)
		signal (SIGINT, SIG_IGN);
	if (signal (SIGHUP, termination_handler) == SIG_IGN)
		signal (SIGHUP, SIG_IGN);
	if (signal (SIGTERM, termination_handler) == SIG_IGN)
		signal (SIGTERM, SIG_IGN);
	if (signal (SIGQUIT, termination_handler) == SIG_IGN)
		signal (SIGQUIT, SIG_IGN);

	/* register exit handler (handles shutdown of core) */
	atexit(exit_handler);

	printf ("Core %s to %s (init=%s)\n", rip, rip, init ? "y" : "n");
	retval = SCTP_CoreUp (rip, rip, init);
	if (retval < 1) {
		fprintf (stderr, "Error occurred. Please check if you have UID 0\n");
		return EXIT_FAILURE;
	}

	xchg(&post_init, 1);

	while (1) {
		int n = scanf("%c",&blubb);
		if (n < 0) break;
		/*TODO: Print core stats*/
		print_core  ();
#ifdef WITH_PACKET_MMAP
		print_sock_stats();
#endif
	}

	return EXIT_SUCCESS;
}


void print_core (void)
{
	struct sctp_core *ad = SCTP_debugcore();
	__s32 tmp;
	int i;
	float ftmp;
	double dtmp;
	static double start_time = 0.0;
	static size_t last_bytes_sent_payload = 0;
	static size_t last_bytes_recv_payload = 0;
	static size_t last_update_time = 0;
	if (ad == NULL) {
		fprintf (stderr, "Core already down.\n");
		return;
	}

	if (start_time < 1.0)
		start_time = shitmytime();

	printf ("****** CORE STATS ******\n");
	printf ("%15lld packets received\n", ad->inter->stats.nr_received);
	ftmp = 100.0*ad->inter->stats.nr_received_payload/ad->inter->stats.nr_received;
	printf ("%15lld payload packets received                    %5.1f%%\n", ad->inter->stats.nr_received_payload, ftmp);
	ftmp = 100.0*ad->inter->stats.nr_protofault/ad->inter->stats.nr_received;
	printf ("%15lld non SCTP packets dropped                    %5.1f%%\n", ad->inter->stats.nr_protofault, ftmp);
	ftmp = 100.0*ad->inter->stats.nr_congdrop/ad->inter->stats.nr_received;
	printf ("%15lld packets lost (buffer full)                  %5.1f%%\n", ad->inter->stats.nr_congdrop, ftmp);
	ftmp = 100.0*ad->inter->stats.nr_outofwin/ad->inter->stats.nr_received;
	printf ("%15lld packets out of window                       %5.1f%%\n", ad->inter->stats.nr_outofwin, ftmp);
	ftmp = 100.0*ad->inter->stats.nr_unknownf/ad->inter->stats.nr_received;
	printf ("%15lld packets with unknown flag                   %5.1f%%\n", ad->inter->stats.nr_unknownf, ftmp);
	printf ("%15lld total payload bytes received\n", ad->inter->stats.bytes_recv_payload);
	ftmp = 100.0*ad->inter->stats.bytes_recv_oow/(ad->inter->stats.bytes_recv_oow+ad->inter->stats.bytes_recv_payload);
	printf ("%15lld total out-of-window bytes received          %5.1f%%\n", ad->inter->stats.bytes_recv_oow, ftmp);
	printf ("%15lld total payload bytes sent\n", ad->inter->stats.bytes_sent_payload);
	ftmp = 100.0*ad->inter->stats.bytes_sent_resend/ad->inter->stats.bytes_sent;
	printf ("%15lld total bytes resent                          %5.1f%%\n", ad->inter->stats.bytes_sent_resend, ftmp);
	printf ("%15lld total bytes sent\n", ad->inter->stats.bytes_sent);
	printf ("%15lld total bytes acked\n", ad->inter->stats.bytes_sent-ad->inter->stats.bytes_sent_resend);
	dtmp = shitmytime();
	ftmp = 1.0e-6 * (ad->inter->stats.bytes_sent_payload - last_bytes_sent_payload) / (dtmp - last_update_time);
	printf ("%15.1f MB/s payload TX rate (since last update)\n", ftmp);
	ftmp = 1.0e-6 * ad->inter->stats.bytes_sent_payload / (dtmp - start_time);
	printf ("%15.1f MB/s payload TX rate (since start up)\n", ftmp);
	ftmp = 1.0e-6 * (ad->inter->stats.bytes_recv_payload - last_bytes_recv_payload) / (dtmp - last_update_time);
	printf ("%15.1f MB/s payload RX rate (since last update)\n", ftmp);
	ftmp = 1.0e-6 * ad->inter->stats.bytes_recv_payload / (dtmp - start_time);
	printf ("%15.1f MB/s payload RX rate (since start up)\n", ftmp);
	printf ("%15lld estimated RTT\n", ad->inter->stats.RTT);
	printf ("************************\n");

	last_bytes_sent_payload = ad->inter->stats.bytes_sent_payload;
	last_bytes_recv_payload = ad->inter->stats.bytes_recv_payload;
	last_update_time = dtmp;

	printf ("\r");
	printf ("CORE: ");
	printf ("txqs: ");
	for (i = 0; i < NUM_QUEUES; i++) {
		tmp = ad->inter->tx_queues[i].nr_full.semval;
		if (tmp < 0) tmp = 0;
		printf ("%3d%%(%5d) ", tmp*100/ad->inter->tx_queues[i].nr_elem, tmp);
	}
	printf ("rxqs: ");
	for (i = 0; i < NUM_QUEUES; i++) {
		tmp = ad->inter->rx_queues[i].nr_full.semval;
		if (tmp < 0) tmp = 0;
		printf ("%3d%%(%5d) ",tmp*100/ad->inter->rx_queues[i].nr_elem, tmp);
	}
	tmp = ad->inter->alloctx.nr_full.semval;
	if (tmp < 0) tmp = 0;
	printf ("freetx: %.3d%%(%d) ",tmp*100/ad->inter->alloctx.nr_elem, tmp);
	tmp = ad->inter->allocrx.nr_full.semval;
	if (tmp < 0) tmp = 0;
	printf ("freerx: %.3d%%(%d) ",tmp*100/ad->inter->allocrx.nr_elem, tmp);
	printf ("lock_mask: %.8x ", ad->inter->lock_mask);
	printf ("RTT: %.lld [us] ", ad->inter->stats.RTT);
	printf ("CTS: %.lld [us] ", ad->currtime);

	tmp = ad->txwin.high_seq - ad->txwin.low_seq;
	if (((ad->txwin.low_seq + MAX_WINSIZ) < MAX_NRFRAMES) && (ad->txwin.high_seq > MAX_NRFRAMES))
		tmp =  ad->txwin.high_seq + MAX_NRFRAMES - ad->txwin.low_seq;
	printf ("TX: %05d-%05d (%4d, ", ad->txwin.low_seq, ad->txwin.high_seq, tmp);
	tmp = 100*ad->txwin.cur_wsize/ad->txwin.max_wsize/sizeof(struct arq_frame);
	printf ("cws %3d%%, ", tmp);
	tmp = 100*ad->txwin.ss_thresh/ad->txwin.max_wsize/sizeof(struct arq_frame);
	printf ("sst %3d%%) ", tmp);

	tmp = ad->rxwin.high_seq - ad->rxwin.low_seq;
	if (((ad->rxwin.low_seq + MAX_WINSIZ) < MAX_NRFRAMES) && (ad->rxwin.high_seq > MAX_NRFRAMES))
		tmp =  ad->rxwin.high_seq + MAX_NRFRAMES - ad->rxwin.low_seq;
	printf ("RX: %05d-%05d (%4d) ", ad->rxwin.low_seq, ad->rxwin.high_seq, tmp);

	printf ("ACK: %05d ", ad->ACK);
	printf ("rACK: %05d ", ad->rACK);
	printf ("REQ: %d ", ad->REQ);
	printf ("\n");
}


void exit_handler (void) {
	xchg(&exiting, 1);
	usleep(100*1000);
	if (post_init) {
		print_core  ();
#ifdef WITH_PACKET_MMAP
		print_sock_stats();
#endif
		SCTP_CoreDown();
	}
}


void termination_handler (int signum) {
	if (cmpxchg(&(exiting), 0, 1) == 0) {
		printf("Termination handler (signal %d) calls exit(EXIT_FAILURE).\n", signum);
		exit(EXIT_FAILURE);
	}
}


int getInterface (const char *ifName, struct ifreq *ifHW) {
	struct ifreq ifreq;
	struct ifconf ifc;
	struct ifreq ifs[MAX_IFS];

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);


	/* get list of interfaces: SIOCGIFCONF */
	ifc.ifc_len = sizeof(ifs);
	ifc.ifc_req = ifs;
	if (ioctl(sockfd, SIOCGIFCONF, &ifc) < 0)
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
		printf("ioctl(SIOCGIFCONF): %m\n");
#pragma GCC diagnostic pop
		close(sockfd);
		return 0;
	}

	/* get hardware address by interface name (ifr_name) */
	strncpy(ifreq.ifr_name, ifName, sizeof(ifreq.ifr_name));
	if (ioctl (sockfd, SIOCGIFHWADDR, &ifreq) < 0) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
		printf("SIOCGIFHWADDR(%s): %m\n", ifreq.ifr_name);
#pragma GCC diagnostic pop
		close(sockfd);
		return 0;
	}

	/* check interface name and return true if ok */
	if (! strcmp(ifName, ifreq.ifr_name)) {
		memcpy(ifHW, &ifreq, sizeof(struct ifreq));
		close(sockfd);
		return 1;
	}

	close(sockfd);
	return 0;
}


void printInterfaces (void) {
	struct ifreq *ifr, *ifend;
	struct ifreq ifreq;
	struct ifconf ifc;
	struct ifreq ifs[MAX_IFS];

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);


	/* get list of interfaces: SIOCGIFCONF */
	ifc.ifc_len = sizeof(ifs);
	ifc.ifc_req = ifs;
	if (ioctl(sockfd, SIOCGIFCONF, &ifc) < 0)
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
		printf("ioctl(SIOCGIFCONF): %m\n");
#pragma GCC diagnostic pop
		close(sockfd);
		return;
	}


	/* iterate through all devices */
	ifend = ifs + (ifc.ifc_len / sizeof(struct ifreq));
	for (ifr = ifc.ifc_req; ifr < ifend; ifr++) {
		if (ifr->ifr_addr.sa_family == AF_INET) {

			/* get hardware address by interface name (ifr_name) */
			strncpy(ifreq.ifr_name, ifr->ifr_name, sizeof(ifreq.ifr_name));
			if (ioctl (sockfd, SIOCGIFHWADDR, &ifreq) < 0) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
				printf("SIOCGIFHWADDR(%s): %m\n", ifreq.ifr_name);
#pragma GCC diagnostic pop
				close(sockfd);
				return;
			}


			/* print every device (ip + hw addr) */
			printf("Device %s\t-> Ethernet address %02x:%02x:%02x:%02x:%02x:%02x\t", ifreq.ifr_name,
					(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[0],
					(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[1],
					(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[2],
					(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[3],
					(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[4],
					(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[5]);
			printf("IP address %s\n", inet_ntoa( ( (struct sockaddr_in *)  &ifr->ifr_addr)->sin_addr));
		}
	}
	close(sockfd);
}

