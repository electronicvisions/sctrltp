#include <sched.h>
#include "sctrltp/sctp_atomic.h"

namespace sctrltp {

void memfence (void)
{
#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
	__asm__ __volatile__    ("mfence;"
							:
							:
							:"memory");
#else
#warning "Unknown arch, memfence doesn't do anything."
#endif
}

__s64 cmpxchg64 (struct atomic_var64 *ptr, __s64 cmp_val, __s64 new_val)
{
	__s64 before;
#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
	__asm__ __volatile__	("lock; cmpxchgq %1,%2;"
							:"=a"(before)
							:"r"(new_val), "m"(ptr->val), "0"(cmp_val)
							:"memory");
#else
#warning "Unknown arch, cmpxchg64 doesn't do anything."
#endif
	return before;
}

/*This function implements the CMPXCHG Instruction with lock prefix*/
__s32 cmpxchg (struct atomic_var *ptr, __s32 cmp_val, __s32 new_val)
{
	__s32 before;
#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
	__asm__ __volatile__ 	("lock; cmpxchgl %1,%2;"
		     	     		:"=a"(before)
		     	     		:"q"(new_val), "m"(ptr->val), "a"(cmp_val)
		             		:"memory");
#else
#warning "Unknown arch, cmpxchg doesn't do anything."
#endif
	return before;
}

/*This function implements the always atomic XCHG operation*/
__s32 xchg (struct atomic_var *ptr, __s32 new_val)
{
	__s32 old;
#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
	__asm__ __volatile__	("xchgl %1,%2;"
							:"=a"(old)
							:"a"(new_val), "m"(ptr->val)
							:"memory");
#else
#warning "Unknown arch, xchgl doesn't do anything."
#endif
	return old;
}

/*atomic dec/inc using CMPXCHG*/

__s32 atomic_dec (struct atomic_var *ptr)
{
	__s32 retval,oldval,newval;
	while (1)
	{
		oldval = ptr->val;
		newval = oldval - 1;
		retval = cmpxchg (ptr, oldval, newval);
		if (retval == oldval) return newval;
	}
}

__s32 atomic_inc (struct atomic_var *ptr)
{
	__s32 retval,oldval,newval;
	while (1)
	{
		oldval = ptr->val;
		newval = oldval + 1;
		retval = cmpxchg (ptr, oldval, newval);
		if (retval == oldval) return newval;
	}
}

/*Important for busy waiting loops (IN KERNEL THERE IS CPU_RELAX())*/
void cpu_relax (void)
{
	__asm__ __volatile__ ( "rep;nop" : : : "memory" );
}

__s32 atomic_read (struct atomic_var *ptr)
{
	return cmpxchg (ptr, 1, 1);
}

void atomic_write (struct atomic_var *ptr, __s32 new_val)
{
	__s32 ret_val,old_val;
	while (1)
	{
		old_val = ptr->val;
		ret_val = cmpxchg (ptr, old_val, new_val);
		if (ret_val == old_val) return;
	}
}

void sem_up (struct atomic_var *sem)
{
	__s32 old_val;
	while (1) {
		old_val = sem->val;
		if (cmpxchg(sem, old_val, old_val+1) == old_val) return;
	}
}

void sem_down (struct atomic_var *sem)
/*NOTE: There is no guarantee that this is starvation free*/
{
	__s32 old_val;
	while (1) {
		/*Wait till sem->val is greater than zero*/
		while ((old_val = sem->val) <= 0) sched_yield();
		/* old_val is now greater than zero and has a defined value
		 * if someone is faster than us, the following cmpxchg will fail*/
		if (cmpxchg(sem, old_val, old_val-1) == old_val) return;
		/*If we come out here we had bad luck*/
	}
}

void cond_wait (struct atomic_var *sem)
{
	while (1) {
		while (sem->val != 1) sched_yield();
		if (cmpxchg(sem, 1, 0) == 1) return;
	}
}

void cond_signal (struct atomic_var *sem, __u32 howmany)
{
	xchg (sem, 1);
}

/*TODO: Implement a fast busy mutex*/
void mutex_lock (struct atomic_var *sem)
{
	sem_down (sem);
	return;
}

void mutex_unlock (struct atomic_var *sem)
{
	sem_up (sem);
	return;
}

void busy_sem_up (struct atomic_var *sem)
{
	sem_up (sem);
	return;
}

void busy_sem_down (struct atomic_var *sem)
{
	sem_down (sem);
	return;
}

} // namespace sctrltp
