#define MTU               1500
#define MIN_PACKET_SIZE      4
#define MIN_PACKET_SEND_SIZE (1*8+12)

#define ETH_HEADER_SIZE     14 /* in bytes */
#define ARQ_HEADER_SIZE      8 /* in bytes */
#define TYPLEN_SIZE          4 /* in bytes */

/*Constraints of hardware*/
#define MAX_WINSIZ         128 /* Can be specified in the range of 1 till (MAX_NRFRAMES-1)/2 (compile-time check exists) */
#define MAX_NRFRAMES       256 /* Maximum number of frames in buffer equals maximum SEQ number + 1 */

#define WIRESPEED          125 /* in 10^6 bytes/sec (because we calculate in us) */

/* static define checks below */
#if MAX_WINSIZ * 2 > MAX_NRFRAMES
	#error "reduce window size to max. seq number / 2 => ((MAX_NRFRAMES-1)/2)!"
#endif

#define PDU_SIZE          (MTU - ETH_HEADER_SIZE - ARQ_HEADER_SIZE)
#define WORD_SIZE            8 /* 64-bit words! */
#define MAX_PDUWORDS       180 /* could be (PDU_SIZE/8)? */

#define UDP_DATA_PORT     1234
#define UDP_RESET_PORT  0xaffe


#define PTYPE_FLUSH        0x8000 /* flush data */
#define PTYPE_LOOPBACK     0x8001 /* loopback data */
#define PTYPE_CFG_TYPE     0x8002 /* configure fpga */
#define PTYPE_SENDDUMMY    0x8003 /* set fpga to send dummy data */
#define PTYPE_STATS        0x8004 /* fpga stats module */
#define PTYPE_DUMMYDATA0   0x0000
#define PTYPE_DUMMYDATA1   0x0001
#define PTYPE_ARQSTAT      0x0002
#define PTYPE_DO_ARQRESET  0x5000
/* FIXME: more PTYPES here... enum unroll maybe? */


/* default hardware (FCP) HostARQ timings/settings */
#define HW_MASTER_TIMEOUT    0x000fffff /* 3000*125 , 3000us is too large! */
#define HW_DELAY_ACK               1023 /* maximum (was 100*125 = 100us */
#define HW_FLUSH_COUNT       0x0000ffff /*  all bits on :) */
#define CFG_SIZE                      3

#define HW_HOSTARQ_MAGICWORD 0xABABABAB
