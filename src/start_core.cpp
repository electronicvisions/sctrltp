/*Program to start SCTP-Core safely*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/ioctl.h>

#include "sctrltp/start_core.h"
#include "sctrltp/us_sctp_core.h"
#include "sctrltp/packets.h"

using namespace sctrltp;

static __s32 post_init = 0;
static __s32 exiting = 0;

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
	retval = SCTP_CoreUp<Parameters<>> (rip, rip, init);
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

namespace sctrltp {

void print_core (void)
{
	print_stats<Parameters<>>();
}


void exit_handler (void) {
	xchg(&exiting, 1);
	usleep(100*1000);
	if (post_init) {
		print_core  ();
#ifdef WITH_PACKET_MMAP
		print_sock_stats();
#endif
		SCTP_CoreDown<Parameters<>>();
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
	strncpy(ifreq.ifr_name, ifName, sizeof(ifreq.ifr_name) - 1);
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
			strncpy(ifreq.ifr_name, ifr->ifr_name, sizeof(ifreq.ifr_name) - 1);
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

} // namespace sctrltp
