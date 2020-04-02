/*This peace of code will test locking methods provided by us_sctp_atomic.c*/

#include "sctrltp/build-config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#include <gtest/gtest.h>

#include "sctrltp/us_sctp_defs.h"
#include "sctrltp/sctp_fifo.h"
#include "sctrltp/nt_memset.h"

#define BUF_SIZE (1 << 25)
#define MAX_ELEM (4*4096)
#define NUM_CPUS 2

using namespace sctrltp;

struct entry {
	__u8 *ptr_to_buf;
	__u8 pad[L1D_CLS - PTR_SIZE];
} __attribute__ ((packed));

struct shared_res {
	struct sctp_fifo full;
	struct entry full_entr[MAX_ELEM];
	__u8   buffer[BUF_SIZE];
	__u32 elemsize;
	__u8   pad[4096-4];
} __attribute__ ((packed, aligned(4096))) shmem;

struct timeval last;
struct timeval curr;

double get_elapsed_time (struct timeval starttime, struct timeval endtime)
{
	double diff;
	diff = ((double)(endtime.tv_sec - starttime.tv_sec)) + ((double)(endtime.tv_usec - starttime.tv_usec))/((double)1000000);
	return diff;
}

void *consumer (void*) {
	__u64 amount = 0;
	struct entry tmp;
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(NUM_CPUS-1, &cpuset);
	pthread_setaffinity_np (pthread_self(), sizeof(cpuset), &cpuset);

	/*Consume 100 MB traffic*/
	while (amount < BUF_SIZE) {
		fif_pop (&(shmem.full), (__u8 *)&tmp, &shmem);
		amount += shmem.elemsize;
	}
	gettimeofday (&curr, NULL);
	pthread_exit (NULL);
}

void *producer (void*) {
	__u64 amount = 0;
	struct entry tmp;
	__u8 *curr_ptr = shmem.buffer;
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	pthread_setaffinity_np (pthread_self(), sizeof(cpuset), &cpuset);

	/*Produce 100 MB traffic*/
	gettimeofday (&last, NULL);
	while (amount < BUF_SIZE) {
		nt_memset64 (curr_ptr, 25, shmem.elemsize);
		tmp.ptr_to_buf = curr_ptr;
		fif_push (&(shmem.full), (__u8 *)&tmp, &shmem);
		amount += shmem.elemsize;
		curr_ptr += shmem.elemsize;
	}
	pthread_exit (NULL);
}


TEST(Fifo, speed)
{
	pthread_t prod, cons;
	double time;
	__u32 elemsize = 1; // TODO: configurable
	__u32 nr = BUF_SIZE/elemsize; // TODO: configurable

	if (nr > MAX_ELEM) nr = MAX_ELEM;

	memset (&shmem, 0, sizeof (struct shared_res));
	shmem.elemsize = elemsize;

	/*Init fifos*/
	fif_init_wbuf (&(shmem.full), nr, sizeof(struct entry), (__u8 *)shmem.full_entr, &shmem);

	pthread_create (&cons, NULL, consumer, NULL);
	pthread_create (&prod, NULL, producer, NULL);

	pthread_join (prod, NULL);
	pthread_join (cons, NULL);

	time = get_elapsed_time(last,curr);
	printf ("#Size     \t#time      \t#Throughput\n");
	printf ("%.8u\t%.8e\t%.8e\n", elemsize, time, BUF_SIZE / time);
	EXPECT_GE(BUF_SIZE/time, 2.4e6);
}
