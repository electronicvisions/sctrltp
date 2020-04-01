#pragma once
/*	\brief This file defines SCTP internal structure
 *
 *	This two functions create and destroy a SCTP-Core
 *	Compile: gcc -Wall -pedantic -c packets.c us_sctp_atomic.c sctp_fifo.c sctp_window.c us_sctp_core.c
 *	For implementation of RTT estimate use -DWITH_RTTADJ
 *	For implementation of congestion avoidance use -DWITH_CONGAV
 */

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <string.h>
#include "us_sctp_timer.h"
#include "us_sctp_defs.h"
#include "packets.h"
#include "sctp_atomic.h"
#include "sctp_window.h"
#include "sctp_fifo.h"
#include "us_sctp_sock.h"

namespace sctrltp {

template <typename P = Parameters<>>
struct sctp_core {
	char const* NAME;                       /* name of core */
	__u32       pad0[L1D_CLS/4 - (sizeof(char const*)/4)];
	struct      empty_cl STATUS;            /*Is read by all threads and modifies their behaviour*/
	__u32       ACK;                        /*Is updated by RX and equals to the last valid sequencenr received*/
	__s32       REQ;                        /*Request bit: 1 ack transmission requested 0 no pending request*/
	__u32       pad1[L1D_CLS/4-2];
	__u32       rACK;                       /*Is updated by RX and equals the last new ACK received*/
	__s32       NEW;                        /*New bit: 1 new remote ACK recvd 0 opposite*/
	__u32       pad2[L1D_CLS/4-2];
	__u64       currtime;                   /*current time (CURRENTLY UPDATED BY RESEND THREAD)*/

	sctp_window<P> txwin;		        /*sliding window of TX/RX*/
	sctp_window<P> rxwin;               /*sliding window of RX*/
	sctp_sock sock;		            /*packet socket structure*/
	sctp_timer txtimer;              /*RTC/HPET timer for TX*/

	sctp_interface<P> *inter;           /*Includes tx_queue, rx_queue, alloc queue and buffer pool (SHARED)*/
	pthread_t	txthr;			            /*The thread ids*/
	pthread_t	rxthr;
	pthread_t	rsthr;
	pthread_t	allocthr;

};
// TODO: also check non-default-parameterized versions
static_assert(offsetof(sctp_core<>, ACK) == (2*L1D_CLS), "");
static_assert(offsetof(sctp_core<>, rACK) == (3*L1D_CLS), "");
static_assert(offsetof(sctp_core<>, currtime) == (4*L1D_CLS), "");

/*Main interface functions to SCTP Core*/

/*This function prepares and start SCTP algorithm
 *returning 1 on success otherwise a negative value*/

template <typename P>
__s8 SCTP_CoreUp (char const *name, char const *rip, __s8 wstartup);

/*Stops algorithm, frees mem and gives statuscode back*/
template <typename P>
__s8 SCTP_CoreDown (void);

/*Gives direct access to inner core structure USE WITH CARE*/
template <typename P>
sctp_core<P> *SCTP_debugcore (void);

/* return seconds since epoch */
double shitmytime();

} // namespace sctrltp
