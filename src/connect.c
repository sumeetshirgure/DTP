#include "gate.h"
#include "packet.h"

#include <arpa/inet.h>		/* inet_aton */

#include <stdlib.h>
#include <string.h>

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

int dtp_listen (dtp_server * server, char *hostname, port_t *port_no) {

  /* Check gate status. */
  if( server->status != IDLE )
    return 1;

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
  ssize_t buflen = 0;
  while( ('0' <= ret[buflen] && ret[buflen] <= '9') ||
	 ret[buflen] == '.' ) buflen++; /* Get length of string. */
  ret[buflen++] = '\0';
  memcpy(hostname, ret, buflen);

  *port_no = ntohs((server->addr).sin_port);

  /* Set up gate resources. */
  return setup_gate(server);
}

int dtp_connect (dtp_client* client) {

  /* Check gate status. */
  if( client->status != IDLE )
    return 1;

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

/* Frees buffers and closes connection. */
int close_dtp_gate (struct dtp_gate * gate) {
  if( gate->status == CONN || gate->status == FINR ) {
    pthread_mutex_lock(&(gate->outbuf_mtx));
    seq_t finno = gate->sndno;

    while( gate->obufsize >= LIM )
      pthread_cond_wait(&(gate->outbuf_var), &(gate->outbuf_mtx));

    make_pkt((gate->outbuf)+(gate->outend),
	     finno,
	     0,
	     gate->outend,
	     0,
	     0,
	     FIN,
	     NULL);
    gate->outend = (gate->outend + 1)%MXW;
    gate->obufsize++;
    pthread_cond_broadcast(&(gate->outbuf_var));

    if( gate->status == CONN ) { /* If connected, wait for acket. */
      while( gate->seqno != finno )
	pthread_cond_wait(&(gate->outbuf_var), &(gate->outbuf_mtx));
    } else {
      gate->status = CLSD;
    }

    pthread_mutex_unlock(&(gate->outbuf_mtx));

    if( gate->status == CONN )
      gate->status = FINS;	/* FIN sent */
  }

  pthread_mutex_lock(&(gate->inbuf_mtx));
  while( gate->status == FINS )
    pthread_cond_wait(&(gate->inbuf_var), &(gate->inbuf_mtx));
  pthread_mutex_unlock(&(gate->inbuf_mtx));

  /* Stop daemons. Where were they hiding? */
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
