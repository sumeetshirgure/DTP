#include "gate.h"
#include "packet.h"

#include <arpa/inet.h>		/* inet_aton */

#include <string.h>

#ifdef DTP_DBG
#include <stdio.h>
#endif

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

/**
   DTP send function. Keeps pushing data into gate's outbuf until
   all data has been sent and is blocked until all of the data has 
   been acknowledged by the receiver.
 */
int dtp_send(struct dtp_gate* gate, const void* data, size_t len) {
  const byte_t * beg = (const byte_t *)data,
    * end = beg + len; /* Convert to byte pointers. */
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
    gate->outend = (gate->outend + 1)%MXW;
    gate->obufsize++;
    beg += blk;
    pthread_cond_broadcast(&(gate->outbuf_var));
    pthread_mutex_unlock(&(gate->outbuf_mtx));
  }
  return 0;
}

/**
   DTP receive function. Waits until receiver buffer has
   at least 1 byte of data. Returns number of bytes read.
 */
size_t dtp_recv(struct dtp_gate* gate, void* data, size_t maxsize) {
  byte_t * beg = (byte_t *) data;
  size_t bytes_read = 0;
  pthread_mutex_lock(&(gate->inbuf_mtx));

  /* Block until receiver buffer is nonempty. */
  while( gate->ibufsize == 0 )
    pthread_cond_wait(&(gate->inbuf_var), &(gate->inbuf_mtx));

  while( maxsize > 0 && gate->ibufsize > 0 ) {
    packet_t *pkt = (gate->inbuf) + (gate->inbeg);
    size_t wr_len = maxsize,
      rem = (pkt->len - gate->byte_offset);
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
    bytes_read += wr_len;
    maxsize -= wr_len;
  }

  pthread_mutex_unlock(&(gate->inbuf_mtx));
  return bytes_read;
}
