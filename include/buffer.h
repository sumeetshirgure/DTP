#ifndef _BUFFER_H
#define _BUFFER_H

#include "types.h"

#include <pthread.h>

/**
  Thread safe bounded packet buffer.
 */
typedef struct dtp_buff {
  packet_t *buff;		/* Packet buffer. */
  size_t buff_size, max_size;	/* Current and maxmimum sizes. */
  size_t inptr, outptr;		/* Pointers to special locations. */
  pthread_mutex_t mtx;		/* POSIX thread mutexes. */
  pthread_cond_t szc;		/* POSIX condition variable. */
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
