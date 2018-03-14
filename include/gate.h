#ifndef _GATE_H
#define _GATE_H

#include "types.h"

#include <pthread.h>		/* POSIX thread library. */
#include <sys/time.h>
#include <time.h>

#include <netinet/ip.h>		/* struct sockaddr_in. */

#define IDLE 0x01
#define CONN 0x02

/**
  dtp_server and dtp_client (also called "gates")
  are encapsulations for a socket coupled with an address.
  They represent a dtp connection.
*/
struct dtp_gate {
  int status;			/* State of this gate. IDLE / CONN. */
  int socket;			/* Socket file descriptor for this gate. */
  struct sockaddr_in self;	/* Self address. */
  struct sockaddr_in addr;	/* Remote address. */

  /* Connection state. */
  struct timespec ackstamp;	/* Timestamp. */
  pthread_mutex_t tm_mtx;	/* Timestamp mutex guard. */
  pthread_cond_t tm_cv;		/* Timestamp semaphore. */

  /* Sequence numbers. */
  seq_t seqno, sndno;		/* Sent sequence numbers. */
  seq_t ackno, ackfr;		/* Acknowledgement metadata. */

  /* Packet buffers. */
  packet_t *inbuf, *outbuf;	 /* Incoming / outgoing data. */

  /* Outgoing data flow control. */
  size_t outbeg, outsnd, outend; /* 3 pointers to outbuf. */
  size_t WND, AXW, SSTH;	/* Windowing variables / threshold. */
  pthread_mutex_t outbuf_mtx;	/* Guards out<var> */
  pthread_cond_t outbuf_var;	/* Guards out<var> */

  /* Incoming data flow control. */
  byte_t *rcvf;			 /* Received flags. */
  size_t inbeg, inend;		 /* Pointers to inbuf. */
  pthread_mutex_t inbuf_mtx;	 /* Guards in<var> */
  pthread_cond_t inbuf_var;	 /* Guards in<var> */

  pthread_t snd_dmn;	 /* Thread handling outgoing packet I/O. */
  pthread_t rcv_dmn;	 /* Thread handling incoming packet I/O. */
  pthread_t tmo_dmn;	 /* Thread handling timeout. */

  /* All daemons have the address of the gate as the pthread argument. */
};

typedef struct dtp_gate dtp_server;
typedef struct dtp_gate dtp_client; /* Same struct, different usage. */

/**
   All functions return 0 on success, and nonzero on any failure.
 */
/* -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- */
/* Server side functions. */
/**
   Create a socket.
   Initialize server with self address and given port.
   (Any address is chosen in case there are mutliple.)
   Pass 0 to listen to any port.
 */
int init_dtp_server (dtp_server*, port_t);

/**
   Listen for client.
   Once connection is established, allocate resources.
 */
int dtp_listen (dtp_server*, char*, port_t*);


/* -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- */
/* Client side functions. */
/**
   Create a socket.
   Initialize client with the address of the server to connect to.
 */
int init_dtp_client (dtp_client*, const char*, port_t);

/**
   Try connecting to the server specified while initialization.
 */
int dtp_connect (dtp_client*);

/* -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- */
/* Data transmission functions. */
/**
   Send data through this gate (either server or client.)
 */
int dtp_send (struct dtp_gate*, const void*, size_t);

/**
   Recieve data from this gate (either server or client.)
   Waits for sender to send at least size bytes.
   Returns once all bytes have been read.
 */
int dtp_recv (struct dtp_gate*, void*, size_t);


/* -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- */

/**
   Close connection.
 */
int close_dtp_gate(struct dtp_gate*);

#endif
