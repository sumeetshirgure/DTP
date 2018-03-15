#ifndef _PACKET_H
#define _PACKET_H

#include "types.h"
#include "gate.h"

#define RCV_OK      0
#define RCV_TIMEOUT 1
#define RCV_WRHOST  2
#define RCV_ERROR   3

/* Returns nonzero if two addresses are different. */
int validate_address (const struct sockaddr_in *,
		      const struct sockaddr_in *);

/**
   Send a packet.
 */
int send_pkt (struct dtp_gate*, const packet_t *);

/**
   Detect a packet. Sets gate address to the recieved address.
   Call when timeout on socket is not set.
 */
int detect_pkt (dtp_server*, packet_t *);

/**
   Receive a packet. Checks if gate address is same as recieved address.
   Returns error code on error / timeout.
 */
int recv_pkt (struct dtp_gate*, packet_t *);

/**
   Create a packet from the data buffer.
   Assumes write length < PAYLOAD.
 */
int make_pkt (packet_t *, seq_t, seq_t, wptr_t, len_t, len_t, flag_t, const void*);

#endif
