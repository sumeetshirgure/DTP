#include "types.h"
#include "packet.h"

#include <string.h>

#include <sys/socket.h>

int send_pkt (struct dtp_gate* gate, const packet_t *packet) {
  static socklen_t socklen = sizeof(struct sockaddr_in);
  ssize_t stat = sendto(gate->socket,
			packet,
			sizeof(packet_t) - PAYLOAD + packet->len,
			0,
			(const struct sockaddr*) &(gate->addr),
			socklen);
  return stat < 0 ? -1 : 0;
}

int recv_pkt (struct dtp_gate* gate, packet_t *packet) {
  static socklen_t socklen = sizeof(struct sockaddr_in);
  ssize_t stat = recvfrom(gate->socket,
			  packet,
			  sizeof(packet_t),
			  0,
			  (struct sockaddr*) &(gate->addr),
			  &socklen);
  return stat < 0 ? -1 : 0;
}

int make_pkt (packet_t *packet,
	      seq_t seq,
	      wptr_t wptr,
	      len_t len,
	      len_t wsz,
	      flag_t flags,
	      const void* buffer) {
  packet->seq = seq;
  packet->wptr = wptr;
  packet->len = len;
  packet->wsz = wsz;
  packet->flags = flags;
  memcpy(packet->data, (byte_t*) buffer, len);
  return 0;
}
