
#ifndef __ASM_GENERIC_MSGBUF_H
#define __ASM_GENERIC_MSGBUF_H

#include <asm/bitsperlong.h>

struct msqid64_ds {
	struct ipc64_perm msg_perm;
	__kernel_time_t msg_stime;	/* last msgsnd time */
#if __BITS_PER_LONG != 64
	unsigned long	__unused1;
#endif
	__kernel_time_t msg_rtime;	/* last msgrcv time */
#if __BITS_PER_LONG != 64
	unsigned long	__unused2;
#endif
	__kernel_time_t msg_ctime;	/* last change time */
#if __BITS_PER_LONG != 64
	unsigned long	__unused3;
#endif
	unsigned long  msg_cbytes;	/* current number of bytes on queue */
	unsigned long  msg_qnum;	/* number of messages in queue */
	unsigned long  msg_qbytes;	/* max number of bytes on queue */
	__kernel_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_pid_t msg_lrpid;	/* last receive pid */
	unsigned long  __unused4;
	unsigned long  __unused5;
};

#endif /* __ASM_GENERIC_MSGBUF_H */
