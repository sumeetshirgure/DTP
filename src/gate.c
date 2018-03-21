#include "gate.h"

#include "packet.h"

#include <string.h>		/* memset */
#include <arpa/inet.h>		/* inet_aton */

#include <sys/time.h>		/* gettimeofday() */
#include <time.h>		/* time(), clock_gettime */

#include <stdlib.h>		/* rand() */
#include <errno.h>

#ifdef DTP_DBG
#include <stdio.h>
#endif

const size_t MXW = (1<<12);	/* 1 + Maximum window size. 4MiB */
const size_t LIM = (1<<12)-1;		/* Safe limit. */
const size_t FUTURE_WINDOW = (1<<11);	/* Maximum disorder. 1MiB */

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
  struct timespec timeout;
  int stat;
  while( 1 ) {
    pthread_mutex_lock(&(gate->outbuf_mtx));
    while( gate->sndsize ==
	   (gate->WND < gate->obufsize ?
	    gate->WND : gate->obufsize) ) {
      if( gate->sndsize > 0 ) { /* Sender window is fully sent. */
	gettimeofday(&(gate->ackstamp), NULL);
	timeout.tv_nsec = gate->ackstamp.tv_usec * 1000;
	timeout.tv_sec = gate->ackstamp.tv_sec + 1;
	stat = pthread_cond_timedwait(&(gate->tm_cv),
				      &(gate->outbuf_mtx),
				      &timeout);
	if(stat == ETIMEDOUT) {
#ifdef DTP_DBG
	  fprintf(stderr, "Timeout detected <%lu, %lu, %lu> (%lu/%lu) (%lu | %lu)\n",
		  gate->outbeg, gate->outsnd, gate->outend,
		  gate->sndsize, gate->obufsize,
		  gate->WND, gate->SSTH);
	  fflush(stderr);
#endif
	  /* Trigger timeout. */
	  gate->SSTH = (gate->SSTH + 1) >> 1; /* Halve ssthresh. */
	  gate->WND = 1;		/* Set current window to 1 packet. */
	  gate->AXW = 0;		/* Set auxiliary window to 0. */
	  gate->outsnd = gate->outbeg;	/* Resend window. */
	  gate->sndsize = 0;
	  pthread_cond_broadcast(&(gate->outbuf_var));
	}
      } else {			/* Wait for next packet to be sent. */
	pthread_cond_wait(&(gate->outbuf_var), &(gate->outbuf_mtx));
      }
    }

#ifdef DTP_DBG
    fprintf(stderr, "Sending outvar=<%lu, %lu, %lu> outsize=(%lu/%lu) outlim=(%lu|%lu) seq=%u\n",
	    gate->outbeg, gate->outsnd, gate->outend,
	    gate->sndsize, gate->obufsize,
	    gate->WND, gate->SSTH, (gate->outbuf[gate->outsnd]).seq);
    fflush(stderr);
#endif

    send_pkt(gate, (gate->outbuf) + (gate->outsnd));
    gate->outsnd = (gate->outsnd + 1) % MXW;

    gate->sndsize++;

    pthread_cond_broadcast(&(gate->outbuf_var));
    pthread_mutex_unlock(&(gate->outbuf_mtx));
  }
  pthread_exit(NULL);
}

/* Handles incoming data packets. */
static void * receiver_daemon (void * arg) {
  struct dtp_gate* gate = (struct dtp_gate *) arg;
  packet_t packet;
  while( 1 ) {
    int dbg_stat;
    if( (dbg_stat = recv_pkt(gate, &packet)) == RCV_WRHOST ) {
#ifdef DTP_DBG
      fprintf(stderr, "Received packet from unknown host.\n");
      fflush(stderr);
#endif
      continue;
    }

    /* Reset timeout. */
    pthread_cond_broadcast(&(gate->tm_cv));

    /* Acknowledgement. */
    if( packet.flags & ACK ) {
      pthread_mutex_lock(&(gate->outbuf_mtx));
      seq_t ack = packet.ack;

#ifdef DTP_DBG
      fprintf(stderr, "Ackrcvd outvar=<%lu, %lu, %lu> outsize=(%lu/%lu) outlim=(%lu|%lu) ((%u))\n",
	      gate->outbeg, gate->outsnd, gate->outend,
	      gate->sndsize, gate->obufsize,
	      gate->WND, gate->SSTH, packet.seq);
      if( (gate->seqno <= gate->sndno ) ?
	  (gate->seqno <= ack && ack <= gate->sndno) :
	  (gate->seqno <= ack || ack <= gate->sndno) ) {
      } else {
	fprintf(stderr, "Out of order ack.\n");
      }
      fflush(stderr);
#endif

      /* Validate sequence number range. */
      if( (gate->seqno <= gate->sndno ) ?
	  (gate->seqno <= ack && ack <= gate->sndno) :
	  (gate->seqno <= ack || ack <= gate->sndno) ) {

	packet_t *pkt;
	while( gate->seqno != ack ) { /* Shift window. */
	  pkt = (gate->outbuf) + (gate->outbeg);
	  gate->seqno = pkt->seq + pkt->len;
	  gate->outbeg = (gate->outbeg + 1) % MXW;
	  gate->obufsize--;

	  if( gate->sndsize == 0 )
	    gate->outsnd = gate->outbeg;
	  else
	    gate->sndsize--;

	  if( gate->WND >= gate->SSTH ) {
	    gate->AXW++;
	    if( gate->AXW == gate->WND ) {
	      gate->AXW = 0;
	      if( gate->WND < LIM ) {
		gate->WND++;	/* Additive increase. */
		gate->SSTH++;
	      }
	    }
	  } else {
	    if( gate->WND < LIM )
	      gate->WND++;	/* Exponential start. */
	  }
	  
	  /* Limit by receiver window size. */
	  if( gate->WND > packet.wsz )
	    gate->WND = packet.wsz; 

	  /* Reset sent size. Ignore sent packets. */
	  if( gate->WND < gate->sndsize ) {
	    gate->outsnd = (gate->outbeg + gate->WND)%MXW;
	    gate->sndsize = gate->WND;
	  }
	}

	pthread_cond_broadcast(&(gate->outbuf_var));
      }

      /* Detect DUPACKS. */
      if( ack == gate->lstack ) {
	gate->ackfr++;
	if( gate->ackfr == 3 ) { /* Detect 3 DUPACKS */
#ifdef DTP_DBG
	  fprintf(stderr, "Triple DUPACK.\n");
	  fflush(stderr);
#endif
	  gate->SSTH = (gate->SSTH + 1) >> 1; /* Halve ssthresh */
	  gate->WND = 1 + gate->WND / 2;      /* Also halve WND.*/
	  gate->AXW = 0;
	  gate->outsnd = gate->outbeg; /* Resend window. */
	  gate->sndsize = 0;
	  pthread_cond_broadcast(&(gate->outbuf_var));
	}
      } else {
	gate->lstack = ack;
	gate->ackfr = 0;
      }
      pthread_mutex_unlock(&(gate->outbuf_mtx));
    }

    if( packet.len > 0 ) {	/* Data. */
      pthread_mutex_lock(&(gate->inbuf_mtx));

      size_t wpt = packet.wptr;

      if( (packet.wptr + MXW - gate->inend)%MXW < FUTURE_WINDOW
	  && gate->rcvf[packet.wptr] == 0
	  && gate->ibufsize < LIM ) { /* Ack only if receiver buffer is nonfull. */

	(gate->rcvf)[packet.wptr] = 1;
	(gate->inbuf)[packet.wptr] = packet;

	packet_t *pkt;
	while( gate->ibufsize < LIM &&
	       (gate->rcvf)[(gate->inend)] == 1 ) {
	  pkt = (gate->inbuf) + (gate->inend);
	  if( gate->ackno != pkt->seq ) {
#ifdef DTP_DBG
	    fprintf(stderr, "Window wrapping...\n");
	    fflush(stderr);
#endif
	    break;
	  }
	  gate->ackno = pkt->seq + pkt->len;
	  gate->inend = (gate->inend + 1) % MXW;
	  gate->ibufsize++;
	  pthread_cond_broadcast(&(gate->inbuf_var));
	}
#ifdef DTP_DBG
	fprintf(stderr, "Datrcvd [%lu, %lu]@%u. Expecting : %u\n",
		gate->inbeg, gate->inend, packet.wptr, gate->ackno);
	fflush(stderr);
#endif
      }

      /* Send cumulative acknowledgement packet. */
      packet.flags = ACK;
      packet.len = 0;
      packet.ack = gate->ackno;
      packet.wsz = MXW - gate->ibufsize; /* Receiver window size. */
      send_pkt(gate, &packet);

      pthread_mutex_unlock(&(gate->inbuf_mtx));
    } /* Data packet. */
  } /* while (1)  */
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
  gate->sndsize = gate->obufsize = 0;
  gate->outbeg = gate->outsnd = gate->outend = 0;
  gate->inbeg = gate->inend = gate->ibufsize = 0;
  gate->WND = 1;		/* Initial window size. */
  gate->SSTH = MXW >> 1;	/* Set initial ssthresh to MXW / 2 */
  gate->AXW = 0;		/* Auxiliary window size.
				   Used to implement congestion avoidance. */
  gate->sndno = gate->seqno;	/* Sent sequence numbers. */
  gate->lstack = gate->ackno;	/* Last acknowledged sequence number. */
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
  stat = pthread_cond_init(&(gate->tm_cv), NULL);
  if( stat != 0 )
    return stat;
  /* Initialize sender daemon. */
  stat = pthread_create(&(gate->snd_dmn), NULL, &sender_daemon, gate);
  if( stat != 0 )
    return stat;
  /* Initialize receiver deamon. */
  stat = pthread_create(&(gate->rcv_dmn), NULL, receiver_daemon, gate);
  return stat;
}

/* Frees buffers and closes connection. */
int close_dtp_gate (struct dtp_gate * gate) {
  /* TODO : Add FIN / FIN|ACK / ACK closure with timeout limit. */
  /* Stop daemons. */
  pthread_cancel(gate->snd_dmn);
  pthread_cancel(gate->rcv_dmn);
  /* Destroy buffers. */
  free(gate->inbuf);
  free(gate->outbuf);
  free(gate->rcvf);
  /* Free mutexes / semaphores. */
  pthread_mutex_destroy(&(gate->outbuf_mtx));
  pthread_cond_destroy(&(gate->outbuf_var));
  pthread_mutex_destroy(&(gate->inbuf_mtx));
  pthread_cond_destroy(&(gate->inbuf_var));
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
    while( gate->obufsize >= LIM )
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
    gate->obufsize++;
    beg += blk;
    pthread_cond_broadcast(&(gate->outbuf_var));
    pthread_mutex_unlock(&(gate->outbuf_mtx));
  }
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
    while( gate->ibufsize == 0 ) /* TODO : Add TCP recv() specific blocking. */
      pthread_cond_wait(&(gate->inbuf_var), &(gate->inbuf_mtx));
    packet_t *pkt = (gate->inbuf) + (gate->inbeg);
    size_t wr_len = end - beg, rem = (pkt->len - gate->byte_offset);
    if( wr_len >= rem ) {
      wr_len = rem;
      /* Write packet data. */
      memcpy(beg, pkt->data + gate->byte_offset, wr_len);
      (gate->rcvf)[gate->inbeg] = 0; /* Clear bit. */
      (gate->inbeg) = (gate->inbeg + 1)%MXW;
      gate->ibufsize--;
      gate->byte_offset = 0;	/* Reset offset. */
      pthread_cond_broadcast(&(gate->inbuf_var));
    } else {
      memcpy(beg, pkt->data + gate->byte_offset, wr_len);
      gate->byte_offset += wr_len;
    }
    beg += wr_len;
    pthread_mutex_unlock(&(gate->inbuf_mtx));
  }
  return 0;
}
