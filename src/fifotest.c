/*This peace of code will test locking methods provided by us_sctp_atomic.c*/

#define _GNU_SOURCE

#include "sctrltp/build-config.h"
/*#include <linux/types.h>*/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#include "sctrltp/us_sctp_defs.h"
#include "sctrltp/sctp_fifo.h"
#include "sctrltp/nt_memset.h"

#define BUF_SIZE (1 << 25)
#define MAX_ELEM (4*4096)
#define NUM_CPUS 2

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

void *consumer () {
	__u64 amount = 0;
	struct entry tmp;
	/*unsigned long mask = (1 << (NUM_CPUS-1));*/
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

void *producer () {
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
		/*memset (curr_ptr, 25, shmem.elemsize);*/
		nt_memset64 (curr_ptr, 25, shmem.elemsize);
		tmp.ptr_to_buf = curr_ptr;
		fif_push (&(shmem.full), (__u8 *)&tmp, &shmem);
		amount += shmem.elemsize;
		curr_ptr += shmem.elemsize;
	}
	pthread_exit (NULL);
}


int main (int argc, char **argv)
{
	pthread_t prod, cons;
	double time;
	__u32 elemsize = 1;
	__u32 nr = BUF_SIZE/elemsize;

	/*printf ("Fifo benchmark started\n");*/

	if (argc < 2) {
		/*printf ("Using element size 1\n");*/
	} else {
		elemsize = (__u32)atoi(argv[1]);
		nr = BUF_SIZE / elemsize;
	}

	if (nr > MAX_ELEM) nr = MAX_ELEM;

	/*printf ("#Elements: %u #Elementsize: %u\n", nr, elemsize);*/

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
	return 0;
}
