#ifndef _TYPES_H
#define _TYPES_H

/* Packet typedefs. */
typedef unsigned char byte_t;	/* 1 byte */
typedef unsigned short len_t;	/* 2 bytes */
typedef unsigned short wptr_t;
typedef unsigned short flag_t;
typedef unsigned int seq_t;	/* 4 bytes */

#define PAYLOAD 1024

/* Flag masks */
#define ACK 0x0001
#define SYN 0x0002
#define FIN 0x0004

typedef struct packet_t {
  seq_t seq;			/* 4 byte sequence number. */
  wptr_t wptr;			/* 2 byte Window pointer. */
  len_t len;			/* 2 byte data size. */
  len_t wsz;			/* 2 byte broadcast window size. */
  flag_t flags;			/* 2 byte flags. */
  byte_t data[PAYLOAD];		/* <=1024 byte data. */
} packet_t;

/* Gate typedefs. */
typedef unsigned short port_t;	/* IPv4 port type. */



#endif
