#ifndef _SCTP_ATOMIC
#define _SCTP_ATOMIC

#include "sctrltp/build-config.h"
#include <linux/types.h>
#include <limits.h>

/*Level 1 Data Cacheline Size HAS to be defined*/
#ifndef L1D_CLS 
	#define L1D_CLS		64
#endif

/*Prevent cacheline sizes < 64 Byte*/
#if (L1D_CLS < 64)
	#undef L1D_CLS
	#define L1D_CLS		64
#endif

#ifndef PTR_SIZE
	#define PTR_SIZE sizeof(void *)
#endif

#define SYNC_TYPE_NONE 0
#define SYNC_TYPE_DREPPER 1
#define SYNC_TYPE_SEM 2
#define SYNC_TYPE_TICKET 4
#define SYNC_TYPE_QUEUE 8
#define SYNC_TYPE_CONDVAR 16

/*Next one could be or'd with previous values and switches blocking/non-blocking behaviour*/
#define SYNC_TYPE_BLOCK 32

typedef volatile __s32 __vs32;

/*dummy for an empty cache line*/
struct empty_cl {
	__vs32 empty[L1D_CLS/4];
} __attribute__ ((packed));

/*famous blocking drepper mutex using futex syscall (NEEDS TO BE PAGE ALIGNED!)*/
struct drepper_mutex {
	__vs32 lock_waiter;
	__vs32 owner;
	__vs32 type;
	__vs32 pad[L1D_CLS/4 - 3];
} __attribute__ ((packed));

/*a semaphore implementation using futex syscall (if blocking, PAGE ALIGNMENT REQUIRED!)*/
struct semaphore {
	__vs32 semval;
	__vs32 lock;
	__vs32 owner;
	__vs32 waiter;
	__vs32 type;
	__vs32 pad[L1D_CLS/4 - 5];
} __attribute__ ((packed));

/*a ticket lock implementation (if blocking, see above)*/
struct ticket_lock {
	__vs32 curr_ticket;
	__vs32 ticket_cnt;
	__vs32 owner;
	__vs32 waiter;
	__vs32 type;
	__vs32 pad[L1D_CLS/4 - 5];
} __attribute__ ((packed));

#define MAX_PROC 255

/*a queue based lock implementation (only busy variant available)*/
struct queue_lock {
	__vs32 place_cnt;
	__vs32 owner;
	__vs32 waiter;
	__vs32 type;
	__vs32 pad[L1D_CLS/4 - 4];
	struct empty_cl place[MAX_PROC];
} __attribute__ ((packed));

/*Some special fences supported by common hardware*/
void memfence (void);
void storefence (void);
void loadfence (void);

/*This function implements the CMPXCHG Instruction with lock prefix*/
__s32 cmpxchg (volatile __s32 *ptr, __s32 cmp_val, __s32 new_val);

/*This function implements the always atomic XCHG operation*/
__s32 xchg (volatile __s32 *ptr, __s32 new_val);

/*atomic dec/inc using CMPXCHG*/

__s32 atomic_dec (volatile __s32 *val);

__s32 atomic_inc (volatile __s32 *val);

/*This function reads an var atomically*/
__s32 atomic_read (volatile __s32 *ptr);

void atomic_write (volatile __s32 *ptr, __s32 new_val);

/*Should not be here MOVE IT TO OTHER LOC OR REMOVE IT, WHEN COMP FOR KERNELSPACE*/
void cpu_relax (void);

/*A fast blocking mutex can be implemented with the following functions (Ulrich Drepper)*/
void mutex_init (volatile struct drepper_mutex *dm);

void mutex_lock (volatile struct drepper_mutex *dm);

/*Gives 1 back, if lock acquired. otherwise zero is returned*/
__s32 mutex_try_lock (volatile struct drepper_mutex *dm);

void mutex_unlock (volatile struct drepper_mutex *dm);

/* A signal mechanism like cond_wait/cond_signal could be implemented as follows*/
void cond_init (volatile struct semaphore *cond_var);

/*Waits on cond_var to have bits in sig_mask set. Before it returns it unsets those bits*/
__s32 cond_wait (volatile struct semaphore *cond_var, __s32 sig_mask);

/*Only tests cond_var but does not sleep. returns all bits which are set according to sig_mask and resets them*/
__s32 cond_test (volatile struct semaphore *cond_var, __s32 sig_mask);

/*Sets bits in cond_var according to sig_mask and wakes howmany waiter (if there are some!)*/
void cond_signal (volatile struct semaphore *cond_var, __s32 sig_mask,  __u32 howmany);

/*Fast userspace spinlocks*/
__s32 spin_try_lock (volatile __s32 *sem);

void spin_lock (volatile __s32 *sem);

void spin_unlock (volatile __s32 *sem);

/*Fast userspace ticket based spin lock*/
void tlock_init (volatile struct ticket_lock *tl);

void tlock_lock (volatile struct ticket_lock *tl);

void tlock_unlock (volatile struct ticket_lock *tl);

/*Fast userspace queue based spinlocks*/
void qspin_init (volatile struct queue_lock *ql);

__s32 qspin_lock (volatile struct queue_lock *ql);

void qspin_unlock (volatile struct queue_lock *ql, __s32 myplace);

/*a general counting semaphore with busy (in userspace even non-busy) wait can be realized using the following funcs*/
void futex_wait (volatile __s32 *ptr, __s32 val);

void futex_wake (volatile __s32 *ptr, __s32 howmany);

void semaph_init (volatile struct semaphore *sem, __s32 val);

void semaph_up (volatile struct semaphore *sem);

void semaph_down (volatile struct semaphore *sem);

void busy_semaph_up (volatile struct semaphore *sem);

void busy_semaph_down (volatile struct semaphore *sem);

#endif
