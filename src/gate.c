#include "gate.h"

#include "packet.h"

#include <string.h>		/* memset */
#include <arpa/inet.h>		/* inet_aton */

#include <time.h>		/* time() */

#include <stdlib.h>		/* rand() */

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
    srand(time(NULL));
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

  /* Set 1 second timeout for data transfer. */
  timeout.tv_sec = 1; timeout.tv_usec = 0;

  stat = setsockopt(server->socket, SOL_SOCKET, SO_RCVTIMEO,
		    &timeout, sizeof(struct timeval));
  if( stat < 0 )
    return -1;

  stat = setsockopt(server->socket, SOL_SOCKET, SO_SNDTIMEO,
		    &timeout, sizeof(struct timeval));
  if( stat < 0 )
    return -1;

  server->status = CONN;

  char * ret = inet_ntoa((server->addr).sin_addr);
  ssize_t buflen = strlen(ret);
  memcpy(hostname, ret, buflen);

  *port_no = ntohs((server->addr).sin_port);

  return 0;
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

  client->status = CONN;

  /* Set self address. */
  socklen_t socklen = sizeof(struct sockaddr_in*);
  getsockname(client->socket,
	      (struct sockaddr*) &(client->self),
	      &socklen);

  return 0;
}
