#ifndef _GATE_H
#define _GATE_H

#include "types.h"
#include "buffer.h"

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

  seq_t seqno, ackno;		/* Sequence numbers. */
  dtp_buff inbuf, outbuf;	/* Input and output buffers. */

  /* Connection state. */
  struct timespec ackstamp;	/* Timestamp of last acknowledged packet. */
  pthread_mutex_t tm_mtx;	/* Timestamp mutex guard. */
  pthread_cond_t tm_cv;		/* Timestamp semaphore. */

  size_t sndptr, WND, ssth;	/* Windowing variables / threshold. */
  pthread_mutex_t wnd_mtx;	/* Packet window mutex guard. */
  pthread_cond_t wnd_cv;	/* Packet window semaphore. */

  pthread_t snd_dmn;	 /* Thread handling outgoing packet buffer I/O. */
  pthread_t rcv_dmn;	 /* Thread handling incoming packet buffer I/O. */
  pthread_t pkt_dmn;	 /* Thread handling packet flow and congestion parameters. */
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
   Read a block of data smaller than the given size.
   Returns the actual size of data read. (Ignores last argument if NULL.)
 */
int dtp_recv (struct dtp_gate*, const void*, size_t, size_t*);


/* -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- -*- */

/**
   Close connection.
 */
int close_dtp_gate(struct dtp_gate*);

#endif
