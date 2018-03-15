#include "gate.h"

#include "packet.h"

#include <string.h>		/* memset */
#include <arpa/inet.h>		/* inet_aton */

#include <time.h>		/* time(), clock_gettime */

#include <stdlib.h>		/* rand() */
#include <errno.h>

#include <stdio.h>

const size_t MXW = 1<<16;	/* 1 + Maximum window size. */

/* Resources to allocate once a connection is established. */
int setup_gate (struct dtp_gate* gate);

int init_dtp_server (dtp_server* server, port_t port_no) {
  int stat = 0;

  /* Create socket. */
  stat = (server->socket) = socket(AF_INET, SOCK_DGRAM, 0);
  if( stat < 0 )
    return -1;

  /* Create self socket address. */
  struct sockaddr_in* host = &(server->self);
  host->sin_family = AF_INET;
  host->sin_port = htons(port_no);
  host->sin_addr.s_addr = INADDR_ANY;
  memset(host->sin_zero, 0, sizeof(host->sin_zero));

  /* Bind to that address. */
  stat = bind(server->socket, (struct sockaddr*) host, sizeof(struct sockaddr_in));
  if( stat < 0 )
    return -1;

  /* Set socket options. */
  int optval = 1;
  stat = setsockopt(server->socket, SOL_SOCKET, SO_REUSEPORT | SO_REUSEADDR,
		    (const void*) &optval, sizeof(int));
  if( stat < 0 )
    return -1;

  server->status = IDLE;

  return 0;
}

int init_dtp_client (dtp_client* client, const char* hostname, port_t port_no) {
  int stat = 0;

  /* Create socket. */
  (client->socket) = socket(AF_INET, SOCK_DGRAM, 0);
  if( (client->socket) < 0 )
    return -1;

  /* Create remote server address. */
  struct sockaddr_in* host = &(client->addr);
  host->sin_family = AF_INET;
  host->sin_port = htons(port_no);
  inet_aton(hostname, &(host->sin_addr));
  memset(host->sin_zero, 0, sizeof(host->sin_zero));

  /* Set socket options. */
  int optval = 1;
  stat = setsockopt(client->socket, SOL_SOCKET, SO_REUSEPORT | SO_REUSEADDR,
		    (const void*) &optval, sizeof(int));
  if( stat < 0 )
    return -1;

  client->status = IDLE;

  return 0;
}

int dtp_listen (dtp_server * server, char *hostname, port_t *port_no) {
  int stat;

  /* Timeout value. */
  struct timeval timeout;

  packet_t synpack;
  while ( 1 ) {			/* Connection not established. */
    /* Clear timeout on socket. */
    timeout.tv_sec = 0; timeout.tv_usec = 0;
    stat = setsockopt(server->socket, SOL_SOCKET, SO_RCVTIMEO,
		      &timeout, sizeof(struct timeval));
    if( stat < 0 )
      return -1;

    stat = detect_pkt(server, &synpack);
    if( stat != RCV_OK )
      return -1;		/* Non timeout error. */

    if( synpack.flags & SYN ) {
      server->ackno = synpack.seq;
    } else {
      continue;			/* Ignore non SYN packet. */
    }

    /* Initial sequence number. */
    srand(time(NULL) ^
	  (server->self).sin_addr.s_addr ^
	  (server->self).sin_port);
    server->seqno = rand();

    make_pkt(&synpack, server->seqno, server->ackno, 0, 0, 0, SYN|ACK, NULL);
    if( send_pkt(server, &synpack) < 0 )
      continue;			/* Failure. */

    /* Set 1 second timeout. */
    timeout.tv_sec = 1; timeout.tv_usec = 0;
    stat = setsockopt(server->socket, SOL_SOCKET, SO_RCVTIMEO,
		      &timeout, sizeof(struct timeval));
    if( stat < 0 )
      return -1;

    stat = recv_pkt(server, &synpack);
    if( stat == RCV_TIMEOUT || stat == RCV_WRHOST ) {
      continue;			/* Reset connection. */
      /* If two clients simultaneuosly try to send SYN requests
	 to DTP server, neither of them will get a connection. */
    } else if( stat != RCV_OK ) {
      return -1;		/* Other error. */
    }

    if( synpack.flags & ACK ) {
      if( synpack.ack != server->seqno )
	continue;		/* Ignore. */
      break;
    }
  }

  /* Clear timeout on socket. */
  timeout.tv_sec = 0; timeout.tv_usec = 0;

  stat = setsockopt(server->socket, SOL_SOCKET, SO_RCVTIMEO,
		    &timeout, sizeof(struct timeval));
  if( stat < 0 )
    return -1;

  stat = setsockopt(server->socket, SOL_SOCKET, SO_SNDTIMEO,
		    &timeout, sizeof(struct timeval));
  if( stat < 0 )
    return -1;

  /* Set connection status. */
  server->status = CONN;

  char * ret = inet_ntoa((server->addr).sin_addr);
  ssize_t buflen = strlen(ret);
  memcpy(hostname, ret, buflen);

  *port_no = ntohs((server->addr).sin_port);

  /* Set up gate resources. */
  return setup_gate(server);
}

int dtp_connect (dtp_client* client) {
  int stat;

  /* Set 1 second timeout on socket. */
  struct timeval timeout;
  timeout.tv_sec = 1; timeout.tv_usec = 0;

  stat = setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO,
		    &timeout, sizeof(struct timeval));
  if( stat < 0 )
    return -1;

  stat = setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO,
		    &timeout, sizeof(struct timeval));
  if( stat < 0 )
    return -1;

  /* Initial sequence number. */
  srand(time(NULL));
  client->seqno = rand();

  packet_t synpack;
  make_pkt(&synpack, client->seqno, 0, 0, 0, 0, SYN, NULL);

  if( send_pkt(client, &synpack) < 0 )
    return -1;

  if( recv_pkt(client, &synpack) != RCV_OK || !(synpack.flags & ACK) )
    return -1;

  if( client->seqno != synpack.ack ) /* Validate sent sequence number. */
    return -1;			     /* Abort connection. */

  client->ackno = synpack.seq;	/* Read initial sequence number. */

  make_pkt(&synpack, 0, client->ackno, 0, 0, 0, ACK, NULL);
  if( send_pkt(client, &synpack) < 0 )
    return -1;

  /* Set connection status. */
  client->status = CONN;

  /* Set self address. */
  socklen_t socklen = sizeof(struct sockaddr_in*);
  getsockname(client->socket,
	      (struct sockaddr*) &(client->self),
	      &socklen);

  /* Clear timeout. */
  timeout.tv_sec = 0; timeout.tv_usec = 0;

  stat = setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO,
		    &timeout, sizeof(struct timeval));
  if( stat < 0 )
    return -1;

  stat = setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO,
		    &timeout, sizeof(struct timeval));
  if( stat < 0 )
    return -1;

  /* Set up gate resources. */
  return setup_gate(client);
}

/**
   Threads for managing tasks.
 */
/* Handles outgoing data packets. */
static void * sender_daemon (void * arg) {
  struct dtp_gate* gate = (struct dtp_gate *) arg;
  while( 1 ) {
    pthread_mutex_lock(&(gate->outbuf_mtx));
    size_t sndsize = (gate->outsnd + MXW - gate->outbeg)%MXW;
    size_t bufsize = (gate->outend + MXW - gate->outbeg)%MXW;
    while( sndsize >= bufsize || sndsize >= gate->WND ) {
      pthread_cond_wait(&(gate->outbuf_var), &(gate->outbuf_mtx));
      sndsize = (gate->outsnd + MXW - gate->outbeg)%MXW;
      bufsize = (gate->outend + MXW - gate->outbeg)%MXW;
    }
    send_pkt(gate, (gate->outbuf) + (gate->outsnd));
    pthread_mutex_lock(&(gate->tm_mtx));
    clock_gettime(CLOCK_MONOTONIC, &(gate->ackstamp));
    pthread_cond_broadcast(&(gate->tm_cv));
    pthread_mutex_unlock(&(gate->tm_mtx));
    gate->outsnd = (gate->outsnd + 1) % MXW;
    pthread_cond_broadcast(&(gate->outbuf_var));
    fprintf(stderr, "Sending [%u, %u, %u] (%u/%u) @%u\r",
	    gate->outbeg, gate->outsnd, gate->outend,
	    sndsize, bufsize, gate->WND); fflush(stderr);
    pthread_mutex_unlock(&(gate->outbuf_mtx));
  }
  pthread_exit(NULL);
}

/* Handles incoming data packets. */
static void * receiver_daemon (void * arg) {
  struct dtp_gate* gate = (struct dtp_gate *) arg;
  packet_t packet;
  while( 1 ) {
    recv_pkt(gate, &packet);
    if( packet.flags & ACK ) {
      pthread_mutex_lock(&(gate->outbuf_mtx));
      size_t ack = packet.ack;
      if( (gate->seqno < ack && ack <= gate->sndno) ||
	  !(gate->sndno < ack && ack <= gate->seqno) ) {
	pthread_cond_broadcast(&(gate->tm_cv)); /* Signal ACK received. */
	packet_t *pkt;
	while( gate->seqno != ack ) {
	  pkt = (gate->outbuf) + (gate->outbeg);
	  gate->seqno = pkt->seq + pkt->len;
	  gate->outbeg = (gate->outbeg + 1) % MXW;
	  if( gate->WND >= gate->SSTH ) {
	    gate->AXW++;
	    if( gate->AXW == gate->WND ) {
	      gate->AXW = 0;
	      if( gate->WND + 1 < MXW )
		gate->WND++;	/* Additive increase. */
	    }
	  } else {
	    if( gate->WND + 1 < MXW )
	      gate->WND++;	/* Exponential start. */
	  }
	  if( gate->WND > packet.wsz )
	    gate->WND = packet.wsz; /* Limit by receiver window size. */
	}
	pthread_cond_broadcast(&(gate->outbuf_var));
      }
      pthread_mutex_unlock(&(gate->outbuf_mtx));
    } /* Acknowledgement. */
    if( packet.len > 0 ) {	/* Data. */
      pthread_mutex_lock(&(gate->inbuf_mtx));
      if( gate->rcvf[packet.wptr] == 0 ) {
	(gate->rcvf)[packet.wptr] = 1;
	(gate->inbuf)[packet.wptr] = packet;
	fprintf(stderr, "Accepting (%u, %u) @%u",
		gate->inbeg, gate->inend, gate->WND); fflush(stderr);
	/* Send cumulative acknowledgement packet. */
	gate->ackno = packet.seq + packet.len;
	packet_t *pkt;
	while( (gate->rcvf)[(gate->inend)] == 1 ) {
	  pkt = (gate->inbuf) + (gate->inend);
	  gate->ackno = pkt->seq + pkt->len;
	  gate->inend = (gate->inend + 1) % MXW;
	}
	packet.flags = ACK;
	packet.len = 0;
	packet.ack = gate->ackno;
	packet.wsz = MXW - 1 -	/* Broadcast window size. */
	  (gate->inend + MXW - gate->inbeg) % MXW;
	send_pkt(gate, &packet);
	pthread_cond_broadcast(&(gate->inbuf_var));
      }
      pthread_mutex_unlock(&(gate->inbuf_mtx));
    }
  }
  pthread_exit(NULL);
}

/* Handles flow and congestion control. */
static void * timeout_daemon (void * arg) {
  struct dtp_gate* gate = (struct dtp_gate *) arg;
  struct timespec timeout;
  while( 1 ) {
    pthread_mutex_lock(&(gate->outbuf_mtx));
    /* Wait for all packets of the window to be sent. */
    size_t sndsize = (gate->outsnd + MXW - gate->outbeg)%MXW;
    size_t bufsize = (gate->outend + MXW - gate->outbeg)%MXW;
    /* Wait till entire window (and at least one packet) is sent. */
    while( bufsize == 0 ||
	   sndsize < gate->WND && sndsize < bufsize ) {
      pthread_cond_wait(&(gate->outbuf_var), &(gate->outbuf_mtx));
      sndsize = (gate->outsnd + MXW - gate->outbeg)%MXW;
      bufsize = (gate->outend + MXW - gate->outbeg)%MXW;
    }
    pthread_mutex_lock(&(gate->tm_mtx));
    timeout = gate->ackstamp;
    timeout.tv_sec += 1;	/* Set 1 second timeout. */
    int stat = pthread_cond_timedwait(&(gate->tm_cv),
				      &(gate->tm_mtx),
				      &timeout);
    if( stat != 0 && errno == ETIMEDOUT ) {
      /* Trigger timeout. */
      gate->SSTH = (gate->SSTH + 1) >> 1; /* Halve ssthresh. */
      gate->WND = 1;		/* Set current window to 1 packet. */
      fprintf(stderr, "\nTimeout detected.\n"); fflush(stderr);
    } else {
      pthread_mutex_unlock(&(gate->tm_mtx));
    }
    pthread_mutex_unlock(&(gate->outbuf_mtx));
  }
  pthread_exit(NULL);
}

/* Sets up buffers and creates threads. */
int setup_gate (struct dtp_gate* gate) {
  /* Initialize buffers. */
  gate->inbuf  = calloc(MXW, sizeof(packet_t));
  gate->outbuf = calloc(MXW, sizeof(packet_t));
  gate->rcvf   = calloc(MXW, sizeof(byte_t));
  if( gate->inbuf == NULL ||
      gate->outbuf == NULL ||
      gate->rcvf == NULL )
    return -1;
  gate->outbeg = gate->outsnd = gate->outend = 0;
  gate->inbeg = gate->inend = 0;
  gate->WND = 1;		/* Initial window size. */
  gate->SSTH = MXW;		/* Set initial ssthresh to MXW */
  gate->AXW = 0;		/* Auxiliary window size.
				   Used to implement congestion avoidance. */
  gate->sndno = gate->seqno;	/* Sent sequence numbers. */
  gate->ackfr = 0;		/* Frequency of last acked sequence number. */
  gate->byte_offset = 0;	/* Byte offset. */
  int stat;
  /* Initialize mutexes and semaphores. */
  stat = pthread_mutex_init(&(gate->outbuf_mtx), NULL);
  if( stat != 0 )
    return stat;
  stat = pthread_cond_init(&(gate->outbuf_var), NULL);
  if( stat != 0 )
    return stat;
  stat = pthread_mutex_init(&(gate->inbuf_mtx), NULL);
  if( stat != 0 )
    return stat;
  stat = pthread_cond_init(&(gate->inbuf_var), NULL);
  if( stat != 0 )
    return stat;
  stat = pthread_mutex_init(&(gate->tm_mtx), NULL);
  if( stat != 0 )
    return stat;
  stat = pthread_cond_init(&(gate->tm_cv), NULL);
  if( stat != 0 )
    return stat;
  /* Initialize sender daemon. */
  stat = pthread_create(&(gate->snd_dmn), NULL, &sender_daemon, gate);
  if( stat != 0 )
    return stat;
  /* Initialize receiver deamon. */
  stat = pthread_create(&(gate->rcv_dmn), NULL, receiver_daemon, gate);
  if( stat != 0 )
    return stat;
  /* Initialize controller deamon. */
  stat = pthread_create(&(gate->tmo_dmn), NULL, timeout_daemon, gate);
  return stat;
}

/* Frees buffers and closes connection. */
int close_dtp_gate (struct dtp_gate * gate) {
  /* TODO : Add FIN / FIN|ACK / ACK closure with timeout limit. */
  /* Stop daemons. */
  pthread_cancel(gate->snd_dmn);
  pthread_cancel(gate->rcv_dmn);
  pthread_cancel(gate->tmo_dmn);
  /* Destroy buffers. */
  free(gate->inbuf);
  free(gate->outbuf);
  free(gate->rcvf);
  /* Free mutexes / semaphores. */
  pthread_mutex_destroy(&(gate->outbuf_mtx));
  pthread_cond_destroy(&(gate->outbuf_var));
  pthread_mutex_destroy(&(gate->inbuf_mtx));
  pthread_cond_destroy(&(gate->inbuf_var));
  pthread_mutex_destroy(&(gate->tm_mtx));
  pthread_cond_destroy(&(gate->tm_cv));
  gate->status = IDLE;
  return 0;
}

/**
   DTP send function. Keeps pushing data into gate's outbuf until
   all data has been sent and is blocked until all of the data has 
   been acknowledged by the receiver.
 */
int dtp_send(struct dtp_gate* gate, const void* data, size_t len) {
  const byte_t * beg = (const byte_t *)data,
    * end = beg + len; /* Convert to byte pointers. */
  size_t expseq;       /* Expected sequence numbers. */
  while( beg != end ) {
    size_t blk = end-beg;
    if( blk > PAYLOAD )
      blk = PAYLOAD;
    pthread_mutex_lock(&(gate->outbuf_mtx));
    /* Wait for space on buffer. */
    while( (gate->outend + MXW - gate->outbeg)%MXW >= MXW - 1 )
      pthread_cond_wait(&(gate->outbuf_var), &(gate->outbuf_mtx));
    make_pkt((gate->outbuf)+(gate->outend),
	     gate->sndno,
	     0,
	     gate->outend,
	     blk,
	     0,
	     0,
	     beg);
    gate->sndno += blk;
    expseq = gate->sndno;
    gate->outend = (gate->outend + 1)%MXW;
    pthread_cond_broadcast(&(gate->outbuf_var));
    pthread_mutex_unlock(&(gate->outbuf_mtx));
    beg += blk;
  }
  /* Wait for ackets. */
  pthread_mutex_lock(&(gate->outbuf_mtx));
  while( gate->seqno != expseq )
    pthread_cond_wait(&(gate->outbuf_var), &(gate->outbuf_mtx));
  pthread_mutex_unlock(&(gate->outbuf_mtx));
  return 0;
}

/**
   DTP receive function. Waits for sender to send at least size
   bytes. Returns once all bytes have been read.
 */
int dtp_recv(struct dtp_gate* gate, void* data, size_t size) {
  byte_t * beg = (byte_t *) data, * end = beg + size;
  while( beg != end ) {
    pthread_mutex_lock(&(gate->inbuf_mtx));
    while( (gate->inend + MXW - gate->inbeg)%MXW == 0 )
      pthread_cond_wait(&(gate->inbuf_var), &(gate->inbuf_mtx));
    packet_t *pkt = (gate->inbuf) + (gate->inbeg);
    size_t wr_len = end - beg, rem = (pkt->len - gate->byte_offset);
    if( wr_len >= rem ) {
      wr_len = rem;
      memcpy(beg, pkt->data, wr_len); /* Write packet data. */
      gate->rcvf[gate->inbeg] = 0; /* Clear bit. */
      gate->inbeg = (gate->inbeg + 1)%MXW;
      gate->byte_offset = 0;	/* Reset offset. */
      pthread_cond_broadcast(&(gate->inbuf_var));
    } else {
      memcpy(beg, pkt->data, wr_len);
      gate->byte_offset += wr_len;
    }
    beg += wr_len;
    pthread_mutex_unlock(&(gate->inbuf_mtx));
  }
  return 0;
}
