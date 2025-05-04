#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications:
   - removed bidirectional GBN code and other code not used by prac.
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE 12      /* Double the window size for SR to avoid ambiguity */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;
  for ( i=0; i<20; i++ )
    checksum += (int)(packet.payload[i]);
  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  return packet.checksum != ComputeChecksum(packet);
}


/********* Sender (A) variables and functions ************/

static struct pkt buffer[SEQSPACE];  /* array for storing packets waiting for ACK */
static bool acked[SEQSPACE]; /*Individual ack tracking */
static int base;                /* the number of packets currently awaiting an ACK */
static int nextseqnum;               /* the next sequence number to be used by the sender */

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  if ((nextseqnum + SEQSPACE - base) % SEQSPACE < WINDOWSIZE) {
    struct pkt sendpkt;
    int i;
    sendpkt.seqnum = nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++) sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    buffer[nextseqnum] = sendpkt;
    acked[nextseqnum] = false;

    tolayer3(A, sendpkt);
    if (base == nextseqnum) starttimer(A, RTT); /* Timer tracks the earliest unacked packet */

    nextseqnum = (nextseqnum + 1) % SEQSPACE;
  } else {
    if (TRACE > 0) printf("Window full, message dropped\n");
    window_full++;
  }
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
  if (!IsCorrupted(packet)) {
    int acknum = packet.acknum;

    if (TRACE > 0)
      printf("[A_input] ACK %d received\n", acknum);

    if (!acked[acknum]) {
      acked[acknum] = true;
      new_ACKs++;
      
      while (acked[base]) {
        stoptimer(A);
        base = (base + 1) % SEQSPACE;
        if (base != nextseqnum) starttimer(A, RTT);
      }
    }
  } else {
    if (TRACE > 0)
      printf("[A_input] Corrupted ACK received\n");
  }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  if (TRACE > 0) printf("Timeout: retransmitting packet %d\n", base);
  tolayer3(A, buffer[base]);
  packets_resent++;
  starttimer(A, RTT);
}



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  int i;
  base = 0;
  nextseqnum = 0;
  for (i = 0; i < SEQSPACE; i++) acked[i] = false;
}



/********* Receiver (B)  variables and procedures ************/

static struct pkt recv_buffer[SEQSPACE];
static bool received[SEQSPACE];
static int expectedseqnum;


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  if (!IsCorrupted(packet)) {
    int seqnum = packet.seqnum;
    struct pkt ackpkt;
    int i;
    
    ackpkt.seqnum = seqnum;
    ackpkt.acknum = seqnum;
    
    for (i = 0; i < 20; i++) ackpkt.payload[i] = '0';
    ackpkt.checksum = ComputeChecksum(ackpkt);

    tolayer3(B, ackpkt); /* Send immediate ACK */

    if (((seqnum + SEQSPACE - expectedseqnum) % SEQSPACE) < WINDOWSIZE) {
      if (!received[seqnum]) {
        received[seqnum] = true;
        recv_buffer[seqnum] = packet;

        while (received[expectedseqnum]) {
          tolayer5(B, recv_buffer[expectedseqnum].payload);
          received[expectedseqnum] = false;
          expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
          packets_received++;
        }
      }
    }
  }
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  int i;
  expectedseqnum = 0;
  for (i = 0; i < SEQSPACE; i++) received[i] = false;
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}
