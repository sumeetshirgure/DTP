#ifndef _PACKET_H
#define _PACKET_H

#include "types.h"
#include "gate.h"

/**
   Send a packet.
 */
int send_pkt (struct dtp_gate*, packet_t *);

/**
   Receive a packet.
 */
int recv_pkt (struct dtp_gate*, packet_t *);

/**
   Create a packet from the data buffer.
   Assumes write length < PAYLOAD.
 */
int make_pkt (packet_t *, seq_t, wptr_t, len_t, len_t, flag_t, const void*);

#endif
