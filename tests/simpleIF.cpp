#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

/* ARQ interface */
#include "sctrltp/us_sctp_if.h"

/* HostARQ type */
#define TEST_TYPE 0x2468

/* number of 64-bit words to send/receive */
#define TOTAL_WORDS 1000

using namespace sctrltp;

template <typename P>
void * thread_sending (void * parm);
template <typename P>
void receiving (void * parm);

/* ARQ handle */
static struct sctp_descr<ParametersFcpBss1> *desc = NULL;

/* global data */
__u64 * gdata;


#define USAGE \
"Usage (test via loopback device, sudo needed because of excessive mmapping):\n" \
"$ sudo bin/start_core <SHM_NAME> 127.0.0.1 127.0.0.1 0\n" \
"$ bin/%s <SHM_NAME>\n"


int main(int argc, char const * argv[]) {
	__s32 ret;
	size_t i;
	pthread_t threadvar;

	/* ARQ shm name as parameter */
	if (argc != 2) {
		fprintf(stderr, USAGE, argv[0]);
		return EXIT_FAILURE;
	}

	desc = SCTP_Open<ParametersFcpBss1>(argv[1]);
	if (!desc) {
		fprintf(stderr, "Error: Could not connect to core\n");
		return EXIT_FAILURE;
	}

	/* create some test data */
	gdata = static_cast<__u64*>(malloc(sizeof(__u64) * TOTAL_WORDS));
	srand(0);
	for (i = 0; i < TOTAL_WORDS; i++)
		gdata[i] = rand();

	/* spawn sending thread and do receive synchronously */
	pthread_create (&threadvar, NULL, thread_sending<ParametersFcpBss1>, desc);
	receiving<ParametersFcpBss1>(desc);

	ret = pthread_join(threadvar, NULL);
	if (ret != 0) {
		fprintf(stderr, "Error: Could not join sending thread\n");
		return EXIT_FAILURE;
	}

	/* now everybody is dead and we can reclaim memory */
	free(gdata);

	ret = SCTP_Close(desc);
	if (ret != 0) {
		fprintf(stderr, "Error: Could not close connection to core\n");
		return EXIT_FAILURE;
	}

	return 0;
}

template <typename P>
void * thread_sending (void * parm) {
	struct sctp_descr<P> *desc = (struct sctp_descr<P> *)parm;
	size_t words_sent = 0, no_frames = 0;
	__s64 ret;
	__u64 * payload;

	while (words_sent < TOTAL_WORDS) {
		payload = &gdata[words_sent];
		__u32 tosend = ((TOTAL_WORDS - 176) > words_sent) ? P::MAX_PDUWORDS : (TOTAL_WORDS - words_sent);
		ret = SCTP_Send<P> (desc, TEST_TYPE, tosend, payload);
		if (ret <= 0) {
			fprintf(stderr, "SCTP_Send failed: %llu\n", ret);
			abort();
		}
		/* TODO: handle ret < tosend here, recall until sum(ret) == tosend */
		words_sent += ret;
		no_frames++;
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	printf("%s: %zu words sent in %zu frames\n", __PRETTY_FUNCTION__, words_sent, no_frames);
#pragma GCC diagnostic pop
	sleep(1);
	pthread_exit(NULL);
}

template <typename P>
void receiving (void * parm) {
	struct sctp_descr<P> *desc = (struct sctp_descr<P> *)parm;
	size_t words_received = 0, no_frames = 0, errors = 0, i;
	__s32 ret;
	__u16 num, type;
	__u64 * resp = static_cast<__u64*>(malloc(sizeof(__u64) * P::MAX_PDUWORDS));

	while (words_received < TOTAL_WORDS) {
		ret = SCTP_Recv<P> (desc, &type, &num, resp);
		if (ret != 0) {
			fprintf(stderr, "SCTP_Recv failed: %u\n", ret);
			abort();
		}
		for (i = 0; i < num; i++) {
			if (gdata[words_received+i] != resp[i]) {
				fprintf(stderr, "%zu: %llu != %llu\n", words_received+i, gdata[words_received+i], resp[i]);
				errors++;
			}
		}
		words_received += num;
		no_frames++;
	}

	free(resp);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	printf("%s: %zu words (errors: %zu) received in %zu packets\n",
		__PRETTY_FUNCTION__, words_received, errors, no_frames);
#pragma GCC diagnostic pop
	sleep(1);
}
