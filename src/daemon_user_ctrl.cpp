#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>
#include "sctrltp/us_sctp_if.h"

using namespace sctrltp;

#define MAX_RESET_WAIT 2
// some helpers
static double mytime() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return 1.0 * now.tv_sec + now.tv_usec / 1e6;
}

static __s32 trigger_reset(struct sctp_descr* desc) {
	struct buf_desc buffer;
	double curr_time;
	double start = 0;
	__u64 magic_word[1] = {htobe64(HW_HOSTARQ_MAGICWORD)};

	fprintf(stderr, "trying to send FPGA reset command...\n");
	acq_buf (desc, &buffer, 0);
	init_buf (&buffer);
	append_words (&buffer, PTYPE_DO_ARQRESET, 1, magic_word);
	send_buf (desc, &buffer, 0);
	send_buf (desc, NULL, MODE_FLUSH);
	printf("FPGA reset command sent");

	start = mytime();
	do {
		recv_buf (desc, &buffer, 0);
		if (sctpreq_get_typ(buffer.arq_sctrl) == PTYPE_CFG_TYPE) {
			return 1;
		}
		curr_time = mytime() - start;
	}while(curr_time < MAX_RESET_WAIT);
	return 0;
}

static void set_timings(struct sctp_descr* desc, __u64* cfg) {
	struct buf_desc buffer;

	acq_buf (desc, &buffer, 0);
	init_buf (&buffer);
	append_words (&buffer, PTYPE_CFG_TYPE, CFG_SIZE, cfg);
	send_buf (desc, &buffer, 0);
	send_buf (desc, NULL, MODE_FLUSH);
	printf("Timing frame sent to FPGA\n");
}

int main(int argc, char* argv[]) {
	struct sctp_descr* desc;
	__u64 cfg[CFG_SIZE];

	if (argc != (CFG_SIZE + 2)) {
		printf("Wrong arguments: <shm_name> <master timeout> <ack delay> <flush count>");
		exit(EXIT_FAILURE);
	}
	int i = 0;
	for(; i < CFG_SIZE; i++) {
		cfg[i] = atoi(argv[i+2]);
	}
	desc = open_conn(argv[1]);

	if (trigger_reset(desc) != 1) {
		printf("No reset response frome FPGA");
		exit(EXIT_FAILURE);
	}
	else{
		printf("FPGA reset successful");
	}
	set_timings(desc, cfg);
	printf("FPGA timings successfully set\n");
	return 1;
}
