#ifndef _BUFFER_H
#define _BUFFER_H

#include "types.h"

#include <pthread.h>
#include <semaphore.h>

/**
  Thread safe bounded packet buffer.
 */
typedef struct dtp_buff {
  packet_t *buff;			/* Packet buffer. */
  size_t buff_in, buff_out, buff_size;	/* Pointers */
  pthread_mutex_t rd_mtx, wr_mtx;	/* POSIX thread mutexes. */
  sem_t size_sem, rem_sem;		/* POSIX semaphores. */
} dtp_buff;

/**
   Initialize byte buffer.
 */
int init_dtp_buff (dtp_buff*, size_t);

/**
   Destroy buffer.
 */
int destroy_dtp_buff (dtp_buff*);

/**
   Write data into buffer.
 */
int push (dtp_buff*, packet_t*);

/**
   Read data from buffer.
 */
int pop (dtp_buff*, packet_t*);

#endif
