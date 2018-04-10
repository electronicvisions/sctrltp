/*This peace of code will test locking methods provided by us_sctp_atomic.c*/

/*#include <linux/types.h>*/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#include "sctrltp/sctp_atomic.h"

struct shared_res {
	/*struct drepper_mutex lock;*/
	/*struct ticket_lock tl;*/
	struct queue_lock ql;
	/*struct semaphore sem;*/
	__u32 sh_count;
	__u32 thrstat[10];
	__u32 thrcnt[10];
	__u32 num;
	__u32 pad[986];
} __attribute__ ((aligned (4096), packed)) shmem;

double get_elapsed_time (struct timeval starttime, struct timeval endtime)
{
	double diff;
	diff = ((double)(endtime.tv_sec - starttime.tv_sec)) + ((double)(endtime.tv_usec - starttime.tv_usec))/((double)1000000);
	return diff;
}

void *thread (void *me) {
	__u32 iam = *((__u32 *)me);
	__u32 mycount = 0;
	__u32 tmp;
	__u32 i;
	__u32 c;

	printf ("Thread %d up\n", iam);

	while (1) {
		/*spin_lock (&(shmem.sem.lock));*/
		/*mutex_lock (&(shmem.lock));*/
		/*busy_sem_down (&(shmem.sem));*/
		/*sem_down (&(shmem.sem));*/
		c = qspin_lock(&(shmem.ql));
		/*tlock_lock (&(shmem.tl));*/
		/*printf ("%d: Enter\n", iam);*/
		/*Critical section*/
		shmem.thrstat[iam] = 1;
		shmem.sh_count++;
		mycount++;
		shmem.thrcnt[iam]++;
		tmp = 0;
		for (i = 0; i < shmem.num; i++) tmp += shmem.thrstat[i];
		if (tmp > 1) {
			printf ("thread %d: loc: %u glo: %u > Locking failed. %d thread(s) in critical section too\n",iam, mycount, shmem.sh_count, tmp-1);
			pthread_exit(NULL);
		}
		shmem.thrstat[iam] = 0;
		/*End of critical section*/
		/*printf ("%d: Leave\n", iam);*/
		/*spin_unlock (&(shmem.sem.lock));*/
		/*mutex_unlock (&(shmem.lock));*/
		/*busy_sem_up (&(shmem.sem));*/
		/*sem_up (&(shmem.sem));*/
		qspin_unlock(&(shmem.ql), c);
		/*tlock_unlock (&(shmem.tl));*/
		if (shmem.thrcnt[iam] >= 100000000) {
			printf ("thread %d: finished counting\n", iam);
			pthread_exit(NULL);
		}
		/*if ((shmem.thrcnt[iam] % 100) == 0) printf ("thread %d: alive!!\n", iam);*/
	}
	return NULL;
}

int main (int argc, char **argv)
{
	pthread_t thr[10];
	__u32 thrvar[10];
	__u32 i = 0;
	__u32 num = 3;
	/*char b = '\0';*/
	struct timeval last;
	struct timeval curr;

	printf ("Locking method test started\n");

	if (argc < 2) {
		printf ("Using 3 threads\n");
	} else num = (__u32)atoi(argv[1]);

	if (num > 10) num = 10;


	memset (&shmem, 0, sizeof (struct shared_res));
	shmem.num = num;
	/*mutex_init (&(shmem.lock));*/
	/*sem_init (&(shmem.sem));*/
	qspin_init (&(shmem.ql));
	/*tlock_init (&(shmem.tl));*/

	gettimeofday (&last, NULL);
	while (i < num) {
		thrvar[i] = i;
		pthread_create (&thr[i], NULL, thread, &thrvar[i]);
		i++;
	}

	/*printf ("Threads started (will enter critical section 3*10^9 times)\n");*/
	/*printf ("Press q + Enter to exit\n");*/
	/*while (shmem.sh_count < 3000000000) sched_yield();*/
	for (i = 0; i < num; i++) pthread_join (thr[i], NULL);
	gettimeofday (&curr, NULL);
	for (i = 0; i < num; i++) {
		printf ("Thr %d %u-times in critical section (lock/unlock per second: %e)\n", i, shmem.thrcnt[i], shmem.thrcnt[i] / get_elapsed_time(last,curr));
	}
	/*scanf ("%c", &b);*/
	/*printf ("count: %u\n", shmem.sh_count);*/
	return 0;
}
