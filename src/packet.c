#include "types.h"
#include "packet.h"

#include <string.h>

#include <sys/socket.h>
#include <errno.h>

// #define PACKET_TRACE

#ifdef PACKET_TRACE
#include <stdio.h>
#endif

/* Returns nonzero if two addresses are different. */
int validate_address (const struct sockaddr_in *addr0,
		      const struct sockaddr_in *addr1) {
  if ( addr0->sin_family != addr1->sin_family )
    return 1;
  if ( addr0->sin_port != addr1->sin_port )
    return 1;
  if ( addr0->sin_addr.s_addr != addr1->sin_addr.s_addr )
    return 1;
  return 0;
}

int send_pkt (struct dtp_gate* gate, const packet_t *packet) {
  static socklen_t socklen = sizeof(struct sockaddr_in);
  ssize_t stat = sendto(gate->socket,
			packet,
			sizeof(packet_t) - PAYLOAD + packet->len,
			0,
			(const struct sockaddr*) &(gate->addr),
			socklen);

#ifdef PACKET_TRACE
  ((packet->flags)&ACK) ?
    (fprintf(stderr, ">>> ACK(%u)\n", packet->ack)) :
    (fprintf(stderr, ">>> DAT(%u)\n", packet->seq)) ;
  fflush(stderr);
#endif

  return stat < 0 ? -1 : 0;
}

int recv_pkt (struct dtp_gate* gate, packet_t *packet) {
  static socklen_t socklen = sizeof(struct sockaddr_in);
  struct sockaddr_in recv_addr;	/* Recieved address. */
  ssize_t stat = recvfrom(gate->socket,
			  packet,
			  sizeof(packet_t),
			  0,
			  (struct sockaddr*) &recv_addr,
			  &socklen);
  if ( stat < 0 ) {
    if( errno != EAGAIN && errno != EWOULDBLOCK )
      return RCV_ERROR;
    return RCV_TIMEOUT;
  }

#ifdef PACKET_TRACE
  ((packet->flags)&ACK) ?
    (fprintf(stderr, "<<< ACK(%u)\n", packet->ack)) :
    (fprintf(stderr, "<<< DAT(%u)\n", packet->seq)) ;
  fflush(stderr);
#endif

  stat = validate_address(&recv_addr, &(gate->addr));
  return stat != 0 ? RCV_WRHOST : RCV_OK;
}

int detect_pkt (dtp_server* server, packet_t *packet) {
  static socklen_t socklen = sizeof(struct sockaddr_in);
  ssize_t stat = recvfrom(server->socket,
			  packet,
			  sizeof(packet_t),
			  0,
			  (struct sockaddr*) &(server->addr),
			  &socklen);
  if ( stat < 0 )
    return RCV_ERROR;
  return RCV_OK;
}

int make_pkt (packet_t *packet,
	      seq_t seq,
	      seq_t ack,
	      wptr_t wptr,
	      len_t len,
	      len_t wsz,
	      flag_t flags,
	      const void* buffer) {
  packet->seq = seq;
  packet->ack = ack;
  packet->wptr = wptr;
  packet->len = len;
  packet->wsz = wsz;
  packet->flags = flags;
  if( len > 0 )
    memcpy(packet->data, (byte_t*) buffer, len);
  return 0;
}
