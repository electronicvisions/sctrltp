/**/
#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/futex.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <assert.h>

#include "sctrltp/sctp_atomic.h"
#include "sctrltp/us_sctp_defs.h"

namespace sctrltp {

void memfence (void)
{
	__asm__ __volatile__    ("mfence;"
							:
							:
							:"memory");
}

void storefence (void)
{
	__asm__ __volatile__    ("sfence;"
							:
							:
							:"memory");
}

void loadfence (void)
{
	__asm__ __volatile__    ("lfence;"
							:
							:
							:"memory");
}

static void barrier(void)
{
	/* clobbering memory (compiler will insert some memory barrier) */
	__asm__ __volatile__ ("":::"memory");
}

/*This function implements the CMPXCHG Instruction with lock prefix*/
__s32 cmpxchg (volatile __s32 *ptr, __s32 cmp_val, __s32 new_val)
{
	__s32 before;
	__asm__ __volatile__	("lock; cmpxchgl %1,%2;"
							:"=a"(before)
							:"q"(new_val), "m"(*ptr), "a"(cmp_val)
							:"memory");
	return before;
}

/*This function implements the always atomic XCHG operation*/
__s32 xchg (volatile __s32 *ptr, __s32 new_val)
{
	__s32 old;
	__asm__ __volatile__	("xchgl %1,%2;"
							:"=a"(old)
							:"a"(new_val), "m"(*ptr)
							:"memory");
	return old;
}

/*atomic dec/inc using CMPXCHG*/

__s32 atomic_dec (volatile __s32 *ptr)
{
	__s32 old_val;
	do {
		old_val = *ptr;
	} while (cmpxchg (ptr, old_val, old_val-1) != old_val);
	return old_val-1;
}

__s32 atomic_inc (volatile __s32 *ptr)
{
	__s32 old_val;
	do {
		old_val = *ptr;
	} while (cmpxchg (ptr, old_val, old_val+1) != old_val);
	return old_val+1;
}

/*Important for busy waiting loops (IN KERNEL THERE IS CPU_RELAX())*/
void cpu_relax (void)
{
	__asm__ __volatile__ ( "rep;nop" : : : "memory" );
}

__s32 atomic_read (volatile __s32 *ptr)
{
	__s32 old_val;
	do {
		old_val = *ptr;
	} while (cmpxchg (ptr, old_val, old_val) != old_val);
	return old_val;
}

void atomic_write (volatile __s32 *ptr, __s32 new_val)
{
	__s32 old_val;
	do {
		old_val = *ptr;
	} while (cmpxchg(ptr, old_val, new_val) != old_val);
}

void futex_wait (volatile __s32 *ptr, __s32 val)
{
	__s32 ret;
	/*Check on page alignment*/
	assert (((__u64)ptr % 4096) == 0);
	ret = syscall (__NR_futex, ptr, FUTEX_WAIT, val, NULL, NULL, NULL);
	if (ret != 0) {
		if (likely(errno == EWOULDBLOCK)) return; /* ok to have variable changed in the meantime ;) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
		fprintf (stderr, "futex_wait terminated with %d (%m)\n", ret);
#pragma GCC diagnostic pop
		/* TODO: We should signal error conditions (return value != void)
		 *       or pthread_exit(0);
		 */
	}
	return;
}

void futex_wake (volatile __s32 *ptr, __s32 howmany)
{
	__s32 ret;
	ret = syscall (__NR_futex, ptr, FUTEX_WAKE, howmany, NULL, NULL, NULL);
	if (unlikely(ret < 0)) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
		fprintf (stderr, "futex_wake returned with %d (%m)\n", ret);
#pragma GCC diagnostic pop
		/* TODO: We should signal error conditions (return value != void)
		 *       or pthread_exit(0);
		 */
	}
	return;
}

void mutex_init (volatile struct drepper_mutex *dm)
{
	assert (!dm->type);
	dm->type = SYNC_TYPE_DREPPER;
	dm->owner = 0;
	dm->lock_waiter = 0;
	memfence();
}

/*Mutex introduced by Ulrich Drepper in his famous paper*/
void mutex_lock (volatile struct drepper_mutex *dm)
{
	__s32 c;
	__vs32 *lock = &(dm->lock_waiter);

	assert (dm->type == SYNC_TYPE_DREPPER);

	/*Is lock free?*/
	if ((c = cmpxchg (lock, 0, 1)) != 0) {
		/*Is there already another waiter? Now there is one*/
		if (c != 2)
			c = xchg (lock, 2);
		/*Wait till we got an unlocked state and set it*/
		while (c != 0) {
			futex_wait (lock, 2);
			c = xchg (lock, 2);
		}
	}
}

__s32 mutex_try_lock (volatile struct drepper_mutex *dm)
{
	__vs32 *lock = &(dm->lock_waiter);

	if (cmpxchg (lock, 0, 1) != 0) return 0;
	return 1;
}

void mutex_unlock (volatile struct drepper_mutex *dm)
{
	__s32 c;
	__vs32 *lock = &(dm->lock_waiter);

	assert (dm->type == SYNC_TYPE_DREPPER);

	/*Decrement lock atomically*/
	while (1) {
		c = *lock;
		if (cmpxchg(lock, c, c-1) == c) break;
	}
	/*Was there at least one waiter?*/
	if (c == 2) {
		*lock = 0;
		futex_wake(lock, 1);
	}
}

void cond_init (volatile struct semaphore *cond_var)
{
	assert (!cond_var->type);
	cond_var->type = SYNC_TYPE_CONDVAR;
	cond_var->waiter = 0;
	cond_var->owner = 0;
	cond_var->lock = 0;
	cond_var->semval = 0;
	memfence();
}

/*Waits on cond_var to have bits in sig_mask set. Before it returns it unsets those bits*/
__s32 cond_wait (volatile struct semaphore *cond_var, __s32 sig_mask)
{
	__s32 c;
	__vs32 *lock = &(cond_var->lock);

	assert (cond_var->type == SYNC_TYPE_CONDVAR);

	spin_lock (lock);
	while (((c = cond_var->semval) & sig_mask) == 0) {
		cond_var->waiter++;
		spin_unlock (lock);

		futex_wait (&(cond_var->semval), c);
		if (unlikely(errno == EINTR)) {
			fprintf (stderr, "futex_wait returned with errno EINTR, aborting...\n");
			abort(); /* if futex fails with interrupt signal => propagate! */
		}

		spin_lock (lock);
		cond_var->waiter--;
	}
	/*At least one bit in sig_mask was set in cond_var*/
	c = cond_var->semval & sig_mask;
	/*Reset those bits which were set before and we waited on*/
	cond_var->semval ^= c;
	spin_unlock (lock);
	/*Return bits which were observed*/
	return c;
}

/*Only tests cond_var but does not sleep. returns all bits which are set according to sig_mask*/
__s32 cond_test (volatile struct semaphore *cond_var, __s32 sig_mask)
{
	__s32 c;
	__vs32 *lock = &(cond_var->lock);

	assert (cond_var->type == SYNC_TYPE_CONDVAR);

	spin_lock (lock);
	c = cond_var->semval & sig_mask;
	/*Reset those bits which were set before*/
	cond_var->semval ^= c;
	spin_unlock(lock);
	return c;
}

/*Sets bits in cond_var according to sig_mask and wakes howmany waiter (if there are some!)*/
void cond_signal (volatile struct semaphore *cond_var, __s32 sig_mask,  __u32 howmany)
{
	__vs32 *lock = &(cond_var->lock);

	assert (cond_var->type == SYNC_TYPE_CONDVAR);

	spin_lock (lock);
	cond_var->semval |= sig_mask;
	if (cond_var->waiter) {
		spin_unlock (lock);
		futex_wake (&(cond_var->semval), howmany);
		return;
	}
	spin_unlock (lock);
}

__s32 spin_try_lock (volatile __s32 *sem)
{
	return (!xchg(sem,1));
}

void spin_lock (volatile __s32 *sem)
{
	barrier();
	if (xchg(sem, 1)) {
		/*Test Test and Set*/
		do {
			/*dirty read on lock*/
			while (*sem) cpu_relax();
		} while (xchg(sem,1));
	}
	/*Lock was released and is now held by us :)*/
}

void spin_unlock (volatile __s32 *sem)
{
	/*Unlock*/
	*sem = 0;
	barrier();
}

void tlock_init (volatile struct ticket_lock *tl)
{
	assert (!tl->type);
	tl->type = SYNC_TYPE_TICKET;
	tl->waiter = 0;
	tl->owner = 0;
	tl->ticket_cnt = 0;
	tl->curr_ticket = 0;
	memfence();
}

void tlock_lock (volatile struct ticket_lock *tl)
{
	__s32 c;

	assert (tl->type == SYNC_TYPE_TICKET);

	barrier();
	/*atomic inc*/
	do {
		c = tl->ticket_cnt;
	} while (cmpxchg(&(tl->ticket_cnt), c, c+1) != c);

	/*Wait for lock*/
	while (tl->curr_ticket != c) cpu_relax();
}

void tlock_unlock (volatile struct ticket_lock *tl)
{
	__s32 c;

	assert (tl->type == SYNC_TYPE_TICKET);

	/*atomic inc*/
	do {
		c = tl->curr_ticket;
	} while (cmpxchg(&(tl->curr_ticket), c, c+1) != c);
	barrier();
}

void qspin_init (volatile struct queue_lock *ql)
{
	__s32 c = 1;

	assert (!ql->type);
	ql->type = SYNC_TYPE_QUEUE;
	ql->waiter = 0;
	ql->owner = 0;
	ql->place_cnt = 0;
	/*First thread can acquire lock*/
	ql->place[0].empty[0] = 0;
	while (c < MAX_PROC) {
		ql->place[c].empty[0] = 1;
		c++;
	}
	memfence();
}

__s32 qspin_lock (volatile struct queue_lock *ql)
{
	__s32 c;
	/*Check on type*/
	assert (ql->type == SYNC_TYPE_QUEUE);

	barrier();
	/*atomic inc with boundary*/
	do {
		c = ql->place_cnt;
	} while (cmpxchg(&(ql->place_cnt), c, ((c+1)%MAX_PROC)) != c);

	/*Wait till holder of lock wakes us*/
	while (ql->place[c].empty[0]) cpu_relax();

	return c;
}

void qspin_unlock (volatile struct queue_lock *ql, __s32 myplace)
{
	assert (ql->type == SYNC_TYPE_QUEUE);

	/*Relock our place in queue*/
	ql->place[myplace].empty[0] = 1;
	/*Unlock next waiter*/
	ql->place[(myplace+1)%MAX_PROC].empty[0] = 0;
	barrier();
}

void semaph_init (volatile struct semaphore *sem, __s32 val)
{
	assert(!sem->type);
	sem->type = SYNC_TYPE_SEM;
	sem->waiter = 0;
	sem->owner = 0;
	sem->semval = val;
	sem->lock = 0;
	memfence();
}

/*First we acquire a lock to protect sem, then we increase value (new value in c!) and wake waiters if necessary*/
void semaph_up (volatile struct semaphore *sem)
{
	__s32 c;
	__vs32 *lock = &(sem->lock);

	spin_lock (lock);
	c = ++sem->semval;
	if ((sem->waiter)&&(c > 0)) {
		spin_unlock (lock);
		futex_wake (&(sem->semval), c);
		return;
	}
	spin_unlock (lock);
}

/*First we acquire lock, check on sem value and release, wait, reacquire if necessary before decreasing sem*/
void semaph_down (volatile struct semaphore *sem)
{
	__s32 c;
	__vs32 *lock = &(sem->lock);

	spin_lock (lock);
	while ((c = sem->semval) <= 0) {
		sem->waiter++;
		spin_unlock (lock);

		futex_wait (&(sem->semval), c);

		spin_lock (lock);
		sem->waiter--;
	}
	sem->semval--;
	spin_unlock (lock);
}

void busy_semaph_up (volatile struct semaphore *sem)
{
	__s32 old_val;
	memfence();
	/*Increment semaphore atomically*/
	do {
		old_val = sem->semval;
	} while (cmpxchg(&(sem->semval), old_val, old_val+1) != old_val);
}

 void busy_semaph_down (volatile struct semaphore *sem)
{
	__s32 old_val;
	memfence();
	do {
		/*Wait till sem->val is greater than zero*/
		while ((old_val = sem->semval) <= 0) sched_yield();
		/* old_val is now greater than zero and has a defined value
		 * if someone is faster than us, the following cmpxchg will fail*/
	} while (cmpxchg(&(sem->semval), old_val, old_val-1) != old_val);
	/*We have decremented sem successfully*/
}

} // namespace sctrltp
