#include "buffer.h"

#include <stdlib.h>

int init_dtp_buff(dtp_buff* buffer, size_t npacks) {
  int stat;

  /* Initialize mutex, conditions and variables. */
  stat = pthread_mutex_init(&(buffer->mtx), NULL);
  if( stat != 0 )
    return stat;

  stat = pthread_cond_init(&(buffer->szc), NULL);
  if( stat != 0 )
    return stat;

  buffer->inptr = 0;
  buffer->outptr = 0;
  buffer->buff_size = 0;
  buffer->max_size = npacks;

  /* Allocate memory. */
  buffer->buff = calloc(npacks, sizeof(packet_t));
  if( buffer->buff == NULL )
    return 1;
  return 0;
}

int destroy_dtp_buff(dtp_buff* buffer) {
  /* Free resources and memory. */
  pthread_mutex_destroy(&(buffer->mtx));
  pthread_cond_destroy(&(buffer->szc));
  free(buffer->buff);
  return 0;
}

int push (dtp_buff* buffer, packet_t* packet) {
  int stat;

  /* Acquire lock. */
  stat = pthread_mutex_lock(&(buffer->mtx));
  if( stat != 0 )
    return stat;

  /* Wait for space in buffer. */
  while( buffer->buff_size == buffer->max_size ) {
    stat = pthread_cond_wait(&(buffer->szc), &(buffer->mtx));
    if( stat != 0 )
      return stat;
  }

  /* Write packet contents. */
  (buffer->buff)[(buffer->inptr)++] = (*packet);
  if( buffer->inptr == buffer->max_size )
    buffer->inptr = 0;		/* Modulo maximum buffer size. */

  /* Update and signal buffer size. */
  (buffer->buff_size)++;
  stat = pthread_cond_broadcast(&(buffer->szc));
  if( stat != 0 )
    return stat;

  /* Release lock. */
  stat = pthread_mutex_unlock(&(buffer->mtx));
  if( stat != 0 )
    return stat;
  return 0;
}

int pop (dtp_buff* buffer, packet_t* packet) {
  int stat;

  /* Acquire lock. */
  stat = pthread_mutex_lock(&(buffer->mtx));
  if( stat != 0 )
    return stat;

  /* Wait for data in buffer. */
  while( buffer->buff_size == 0 ) {
    stat = pthread_cond_wait(&(buffer->szc), &(buffer->mtx));
    if( stat != 0 )
      return stat;
  }

  /* Read contents into packet. */
  (*packet) = (buffer->buff)[(buffer->outptr)++];
  if( buffer->outptr == buffer->max_size )
    buffer->outptr = 0;		/* Modulo maximum buffer size. */

  /* Update and signal buffer size. */
  (buffer->buff_size)--;
  stat = pthread_cond_broadcast(&(buffer->szc));
  if( stat != 0 )
    return stat;

  /* Release lock. */
  stat = pthread_mutex_unlock(&(buffer->mtx));
  if( stat != 0 )
    return stat;
  return 0;
}
