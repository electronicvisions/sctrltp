/* 
	This headerfile implements a very special function 
	
	author: M.Schilling, F9
	year: 2009
	
	TODO:
	
	Do not define a fixed CLS value, instead compile with -DCLS=$(getconf LEVEL1_DCACHE_LINESIZE)
*/

#ifndef _IA64_MSET_NOCACHE
	#define _IA64_MSET_NOCACHE

#define CLS 64 /* This is the cache line size of the L1 Data cache (UGLY, but a too high value is not so bad)*/

#include <stdlib.h> /* This libary is needed for posix_memalign */
#include <stdint.h>

/* This funtion allocates a mem area with 8-Byte alignment (needed by MOVNTI)*/
void* malloc_safe (size_t size)
{
	int ret;
	void *tmp = NULL;
	ret = posix_memalign (&tmp, 8, size);
	if (ret < 0) tmp = NULL;
	return tmp;
}

/* This is a special funtion only available on IA64 architectures
   AMD-CPUs might have a problem with this!!!*/
/*inline*/ void nt_memset64 (unsigned char* ptr_aligned, unsigned char c, size_t nr)
{
	unsigned char* tmp = ptr_aligned;
	unsigned int bytes_written = 0;
	
	uint64_t dummy;
	uint64_t c2 = c;
	
	dummy = (c2 << 56)+(c2 << 48)+(c2 << 40)+(c2 << 32)+(c2 << 24)+(c2 << 16)+(c2 << 8)+c2;
	
	if (nr >= CLS)
	{
		while (nr-bytes_written >= CLS)
		{
		/*We want to use Write combining so lets write a whole cache line*/
		__asm__ __volatile__(	"movnti %1, 0(%0); \n"	/*non-temporal mov instruction*/
								"movnti %1, 8(%0); \n"
								"movnti %1, 16(%0);\n"
								"movnti %1, 24(%0);\n"
								"movnti %1, 32(%0);\n"
								"movnti %1, 40(%0);\n"
								"movnti %1, 48(%0);\n"
								"movnti %1, 56(%0)"
							:							/*output values*/
							:	"rc"(tmp),"ra"(dummy)	/*input values*/
							:	"rcx","rax","memory"	/*clobbered regs/mem*/ 
							);
		tmp += CLS;
		bytes_written += CLS;
		}
	
	/* We need to make sure, that all movnti are finished, so lets implement an sfence */
	
		__asm__ __volatile__(	"sfence"
							:
							:
							:	"memory"
							);
	}
	
	/* There could be unwritten bytes (<CLS) now and we will write them with normal movs.
	   Here we would make at least one cache-line dirty, but its ok :)*/
	if ((nr-bytes_written) > 0) {
		memset (tmp, c, (nr-bytes_written));
	}
}

#endif
