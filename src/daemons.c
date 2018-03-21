#include "gate.h"
#include "packet.h"

#include <errno.h>

#ifdef DTP_DBG
#include <stdio.h>
#endif

/* Handles outgoing data packets. */
void * sender_daemon (void * arg) {
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
void * receiver_daemon (void * arg) {
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

      if( (wpt + MXW - gate->inend)%MXW < FUTURE_WINDOW
	  && gate->rcvf[wpt] == 0
	  && gate->ibufsize < LIM ) { /* Ack only if receiver buffer is nonfull. */

	(gate->rcvf)[wpt] = 1;
	(gate->inbuf)[wpt] = packet;

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
		gate->inbeg, gate->inend, wpt, gate->ackno);
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
