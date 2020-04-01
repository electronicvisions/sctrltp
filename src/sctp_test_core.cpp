/**/

#include <arpa/inet.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <linux/types.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include "sctrltp/packets.h"
#include "sctrltp/sctp_atomic.h"
/*#include "sctrltp/us_sctp_core.h"*/
#include "sctrltp/us_sctp_if.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

__u8 mythrstate = 1;

// in words (*8)
#define TEST_SIZE 1000ull*1000*1000*1000
#define PLOT_TIME 1.0 //seconds

#define INT0 "20" // HPET
#define INT1 "47" // NIC

using namespace sctrltp;

double get_elapsed_time (struct timeval starttime, struct timeval endtime)
{
	double diff;
	diff = ((double)(endtime.tv_sec - starttime.tv_sec)) + ((double)(endtime.tv_usec - starttime.tv_usec))/((double)1000000);
	return diff;
}

double mytime() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return 1.0 * now.tv_sec + now.tv_usec / 1e6;
}

double myavg(double vals[], size_t len) {
	double sum = 0.0;
	size_t i;
	for (i = 0; i < len; i++)
		sum += vals[i];
	return sum/len;
}

double mydev(double vals[], size_t len) {
	double mean=0.0, sum_deviation=0.0;
	size_t i;
	for(i = 0; i < len; ++i)
		mean += vals[i];
	mean = mean/len;
	for(i = 0; i < len; ++i)
		sum_deviation += (vals[i] - mean) * (vals[i] - mean);
	return sqrt(sum_deviation/len);
}

void get_intr_cnt(size_t * net, size_t * timer) {
	FILE *fp;
	char path[35];

	// pinned to core 0 (hopefully...)
	//fp = popen("cat /proc/stat | grep -h ctxt | awk '{print $2}' 2>&1", "r");
	fp = popen("sed -n 's/\\s*\\(" INT0 "\\|" INT1 "\\):\\s\\+\\([0-9]\\+\\)\\s\\+.*$/\\2/p' /proc/interrupts 2>&1", "r");
	if (fp == NULL) {
		printf("Failed to run command\n" );
		exit(1);
	}

	/* Read the output a line at a time - output it. */
	if (fgets(path, sizeof(path), fp) != NULL)
		sscanf(path, "%zu\n", timer);
	if (fgets(path, sizeof(path), fp) != NULL)
		sscanf(path, "%zu\n", net);

	/* close */
	pclose(fp);
}


void *sending (void *ret)
{
	__u64 i, max = TEST_SIZE;
	struct timeval begin, finish;

	size_t no_speeds = 0, no_prints = 0;
	size_t const MAX_NO_PRINTS = 2000; // 20 seconds

	double speeds[MAX_NO_PRINTS];
	double pspeeds[MAX_NO_PRINTS];
	double interrupts[MAX_NO_PRINTS];
	double interrupts2[MAX_NO_PRINTS];
	size_t last_interrupts, last_interrupts2;
	get_intr_cnt(&last_interrupts, &last_interrupts2);

	struct sctp_descr *desc = (struct sctp_descr *)ret;
	struct buf_desc buffer;

	/*struct sctrl_cmd cmd[PARALLEL_FRAMES*MAX_CMDS];*/
	/*__u32 i;*/

	/*i = 0;*/
	/*while (i < (PARALLEL_FRAMES * MAX_CMDS)) {*/
		/*[>Read values of modul 3 on BALU<]*/
		/*cmd[i] = sctrl_set_cmd (10,3,i%1024,0);*/
		/*i++;*/
	/*}*/

	printf ("sending thread up\n");
	gettimeofday(&begin, NULL);

	size_t j = 0;
	__u64 packet[MAX_PDUWORDS] = {0};
	__u64 last_i = 0;

	acq_buf (desc, &buffer, 0);
	init_buf (&buffer);
	packet[0] = htobe64(0x80000000100000a0); // 4 = stats, 8 = dummy data, c both...
	append_words (&buffer, 0x0123, 1, packet);
	send_buf (desc, &buffer, MODE_FLUSH);
	//send_buf (desc, NULL, MODE_FLUSH);

	double starttime = mytime();
	double last_print = starttime;

	//while (1) {}

	size_t wordsize = 176;
	size_t packet_cnt = 0;
	size_t packet_cnt_last = 0;
	for (i = 0; i < max;) {
		/*Get free buffer for sending*/
		acq_buf (desc, &buffer, 0);
		/*Set up buffer*/
		init_buf (&buffer);

		//wordsize %= (MAX_PDUWORDS-1);
		//wordsize++;
		//wordsize = 1 + (rand_r(&seedilidi2) % (MAX_PDUWORDS-1));
		//wordsize = 32 * (1 + (rand_r(&seedilidi2) % 5)); //(MAX_PDUWORDS-1));
		assert(wordsize > 0 && wordsize <= MAX_PDUWORDS);
		//static size_t wordsize = 0; //(rand_r(&seedilidi2) % (MAX_PDUWORDS-64)) + 64;
		//if (wordsize < 64)
		//	wordsize++;

		// fill packet
		for(j = 0; j < wordsize; j++) {
		//for(j = 0; j < 100; j++) {
			packet[j] = 0; //htobe64(i);
			i++;
			if (i >= max)
				break;
		}
		//__s32 ret = append_words (&buffer, PTYPE_LOOPBACK/*0xabcd*/, j, packet);
		__s32 ret = append_words (&buffer, PTYPE_DUMMYDATA0, j, packet);
		if (ret <= 0)
			break;

		//do {
		//	__u64 dat = htobe64(i);
		//	__s32 ret = append_words (&buffer, 0xabcd, 1, &dat);
		//	//__s32 ret = append_words (&buffer, PTYPE_LOOPBACK, 1, &dat);
		//	if (ret <= 0 || ++i >= max)
		//		break;
		//} while(true);

		/*push buffer to tx_queue*/
		send_buf (desc, &buffer, 0);
		packet_cnt++;
		//printf("sent %d\n", i);


		//acq_buf (desc, &buffer, 0);
		//init_buf (&buffer);
		//ret = append_words (&buffer, 0xabcd, j, packet);
		//if (ret <= 0)
		//	break;
		//send_buf (desc, &buffer, 0);




		//printf("sent %d\n", i);
		double diff_time = mytime() - last_print;
		double diff_time_start = mytime() - starttime;
		if (diff_time > PLOT_TIME) {
			double rate = 1.0 * (i-last_i) * WORD_SIZE / diff_time;
			size_t new_interrupts, new_interrupts2;
			get_intr_cnt(&new_interrupts, &new_interrupts2);
			if (new_interrupts < last_interrupts) // wrapped around!
				new_interrupts += UINT32_MAX;
			if (new_interrupts2 < last_interrupts2) // wrapped around!
				new_interrupts2 += UINT32_MAX;
			double irate = 1.0 * (new_interrupts - last_interrupts) / diff_time;
			double irate2 = 1.0 * (new_interrupts2 - last_interrupts2) / diff_time;
			last_interrupts = new_interrupts;
			last_interrupts2 = new_interrupts2;
			printf("tx words %12lld = %7.3fGB, tx rate = %7.3fMB/s\n", i, 1e-9*WORD_SIZE*i, 1e-6*rate);
			printf("irate is = %.2fintr/s\n", irate);
			printf("irate2 is = %.2fintr/s\n", irate2);
			printf("diff_time is %f\n", diff_time);
			double prate = 1.0 * (packet_cnt-packet_cnt_last) / diff_time;
			printf("pckt rate is = %.2fpckts/s\n", prate);
			last_i = i;
			last_print = last_print + diff_time;
			packet_cnt_last = packet_cnt;

	
			// drop first 2s
			if (diff_time_start > 2.0) {
				interrupts[no_speeds] = irate;
				interrupts2[no_speeds] = irate2;
				speeds[no_speeds] = rate;
				pspeeds[no_speeds] = prate;
				no_speeds++;
			}
			no_prints++;
		}
		if ((no_prints > MAX_NO_PRINTS) || ((PLOT_TIME * MAX_NO_PRINTS) < (mytime() - starttime)))
			break;
	}
	// flush :)
	send_buf (desc, NULL, MODE_FLUSH);

	gettimeofday(&finish, NULL);

	//printf("sent %lld in %fs = %fMB/s\n", i, get_elapsed_time(begin, finish), 0.000001*WORD_SIZE*i/get_elapsed_time(begin, finish));
	printf("\n\n");
	
	printf("interrupt rate = %.2f +/- %.2fintr/s\n", myavg(interrupts, no_speeds), mydev(interrupts, no_speeds));
	printf("interrupt2 rate = %.2f +/- %.2fintr/s\n", myavg(interrupts2, no_speeds), mydev(interrupts2, no_speeds));
	printf("packet rate = %.2f +/- %.2fpckts/s\n", myavg(pspeeds, no_speeds), mydev(pspeeds, no_speeds));
	printf("sent avg = %f +/- %fMB/s\n", myavg(speeds, no_speeds)/1024/1024, mydev(speeds, no_speeds)/1024/1024);
	//dev(speeds);

	sleep(1);

	pthread_exit (NULL);
}


int main (int argc, char **argv)
{
	struct buf_desc buffer;
	__u64 i, sum = 0, last_sum = 0;
	/*__u8 sflags,num;*/
	/*__u16 nat;*/
	struct sctp_descr *desc = NULL;

	pthread_t threadvar;
	
	unsigned int seedilidi = 423;

	/*Usage*/
	if (argc < 2) {
		fprintf (stderr, "Usage: %s <corename>\n", argv[0]);
		return 1;
	}

	printf ("****Testing Core (make sure, that testbench is running on tap0, Core on tap1)\n");
	desc = open_conn (argv[1]);
	if (!desc) {
		printf ("Error: make sure Core and testbench are up\n");
		return 1;
	}

	printf ("****Sending infinite Packets to BALU\n");
	pthread_create (&threadvar, NULL, sending, desc);

	printf ("****Thread started\n");
	__u64 data = 0;
	__u64 reported = 0;
	__u64 rx_cnt = 0, rx_drop = 0, rx_abort = 0, al_word = 0;
	bool rx_corrupt = false, udp_corrupt = false;
	__s64 rx_corrupt_idx = 0;

	size_t packet_cnt = 0;
	size_t packet_cnt_last = 0;

	size_t no_speeds = 0, no_prints = 0;
	size_t const MAX_NO_PRINTS = 2000; // 20 seconds

	double speeds[MAX_NO_PRINTS];
	double pspeeds[MAX_NO_PRINTS];
	double interrupts[MAX_NO_PRINTS];
	double interrupts2[MAX_NO_PRINTS];
	size_t last_interrupts, last_interrupts2;
	get_intr_cnt(&last_interrupts, &last_interrupts2);

	double starttime = mytime();
	double last_print = starttime;
	while (1) {
		/*Get buffer from rx_queue*/
		recv_buf (desc, &buffer, 0);
		/*Check content of buffer*/
		switch (sctpreq_get_typ(buffer.arq_sctrl)) {
			case PTYPE_ARQSTAT:
				assert(sctpreq_get_len(buffer.arq_sctrl) >= 5);
				rx_cnt     = be64toh(buffer.payload[0]);
				rx_drop    = be64toh(buffer.payload[1]);
				rx_abort   = be64toh(buffer.payload[2]);
				al_word    = be64toh(buffer.payload[3]);
				rx_corrupt_idx = be64toh(buffer.payload[4]) << 2 >> 2;
				rx_corrupt  = (be64toh(buffer.payload[4]) >> 62) & 0x2;
				udp_corrupt = (be64toh(buffer.payload[4]) >> 62) & 0x1;
				break;
			case PTYPE_DUMMYDATA0:
			case PTYPE_DUMMYDATA1:
				{
					packet_cnt++;
					for(i = 0; i < sctpreq_get_len(buffer.arq_sctrl); i++) {
						__u64 dat = be64toh(buffer.payload[i]);
						if (data != dat) {
							//printf("corrupted @%2lld (len: %2d): %lld vs %lld\n", i, sctpreq_get_len(buffer.arq_sctrl), data, dat);
							//data = dat;
							reported++;
						}
						data++;
						static bool first = true;
						if (first) {
							printf("%lu\n", be64toh(buffer.payload[i]));
							first = false;
						}
					}
					sum += sctpreq_get_len(buffer.arq_sctrl);
				}
				break;
			case PTYPE_LOOPBACK:
				{
					for(i = 0; i < sctpreq_get_len(buffer.arq_sctrl); i++) {
						data = ((rand_r(&seedilidi)*1ull) << 32) | rand_r(&seedilidi);
						__u64 dat = be64toh(buffer.payload[i]);
						if (data != dat) {
							//printf("corrupted @%2lld (len: %2d): %lld vs %lld\n", i, sctpreq_get_len(buffer.arq_sctrl), data, dat);
							//data = dat;
							reported++;
						}
						static bool first = true;
						if (first) {
							printf("%lu\n", be64toh(buffer.payload[i]));
							first = false;
						}
					}
					sum += sctpreq_get_len(buffer.arq_sctrl);
				}
				break;
			default:
				printf("corrupted type %x received\n", sctpreq_get_typ(buffer.arq_sctrl));
				break;
		}
		//printf("sum is %d\n", sum);
		/*Release buffer to be reused*/
		rel_buf (desc, &buffer, 0);

		double diff_time_start = mytime() - starttime;
		double diff_time = mytime() - last_print;
		if (diff_time > PLOT_TIME) {
			double corrupt_percentage = 100.0*reported/sum;
			double rate = 1.0 * (sum - last_sum) * WORD_SIZE / diff_time;

			printf("received %12lld words = %7.3fGB, corrupt %12lld words = %7.2f%%, rx rate = %7.3fMB/s\n", sum, 1e-9*WORD_SIZE*sum, reported, corrupt_percentage, 1e-6*rate);
			if (rx_cnt || rx_drop || rx_abort || al_word || rx_corrupt || udp_corrupt)
				printf("rx_cnt %lld, rx_drop %lld, rx_abort %lld, al_word %lld, rx_corrupt %d, udp_corrupt %d, rx_corrupt_idx %lld\n", rx_cnt, rx_drop, rx_abort, al_word, rx_corrupt, udp_corrupt, rx_corrupt_idx);
			last_sum = sum;

			size_t new_interrupts, new_interrupts2;
			get_intr_cnt(&new_interrupts, &new_interrupts2);
			if (new_interrupts < last_interrupts) // wrapped around!
				new_interrupts += UINT32_MAX;
			if (new_interrupts2 < last_interrupts2) // wrapped around!
				new_interrupts2 += UINT32_MAX;
			double irate = 1.0 * (new_interrupts - last_interrupts) / diff_time;
			double irate2 = 1.0 * (new_interrupts2 - last_interrupts2) / diff_time;
			last_interrupts = new_interrupts;
			last_interrupts2 = new_interrupts2;
			double prate = 1.0 * (packet_cnt-packet_cnt_last) / diff_time;
			last_print = last_print + diff_time;
			packet_cnt_last = packet_cnt;

			// drop first 5s
			if (diff_time_start > 5.0) {
				interrupts[no_speeds] = irate;
				interrupts2[no_speeds] = irate2;
				speeds[no_speeds] = rate;
				pspeeds[no_speeds] = prate;
				no_speeds++;
			}
			no_prints++;
			// break after 20s
			if ((no_prints > MAX_NO_PRINTS) || ((PLOT_TIME * MAX_NO_PRINTS) < (mytime() - starttime)))
				break;
		}
	}

	printf("\n\n");

	printf("interrupt rate = %.2f +/- %.2fintr/s\n", myavg(interrupts, no_speeds), mydev(interrupts, no_speeds));
	printf("interrupt2 rate = %.2f +/- %.2fintr/s\n", myavg(interrupts2, no_speeds), mydev(interrupts2, no_speeds));
	printf("packet rate = %.2f +/- %.2fpckts/s\n", myavg(pspeeds, no_speeds), mydev(pspeeds, no_speeds));
	printf("recv avg = %f +/- %fMB/s\n", myavg(speeds, no_speeds)/1024/1024, mydev(speeds, no_speeds)/1024/1024);

	//printf("received %lld\n", sum);

	sleep(1);

	return 0;
}
