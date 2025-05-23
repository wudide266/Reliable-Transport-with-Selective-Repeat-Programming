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
  int checksum = packet.seqnum + packet.acknum;
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
static bool timer_running = false; /* New flag for timer status*/

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  if ((nextseqnum + SEQSPACE - base) % SEQSPACE < WINDOWSIZE) {
    struct pkt sendpkt;
    int i;
    sendpkt.seqnum = nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++) 
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    buffer[nextseqnum] = sendpkt;
    acked[nextseqnum] = false;

    if (TRACE > 0) {
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");
      printf("Sending packet %d to layer 3\n", nextseqnum);
    }

    tolayer3(A, sendpkt);
    if (base == nextseqnum && !timer_running) {
      starttimer(A, RTT);
      timer_running = true;
    }

    nextseqnum = (nextseqnum + 1) % SEQSPACE;
  } else {
    if (TRACE > 0) printf("----A: New message arrives, send window is full\n");
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

    if (TRACE > 0) {
      printf("----A: uncorrupted ACK %d is received\n", acknum);
      printf("----A: ACK %d is not a duplicate\n", acknum);
    }

    if (!acked[acknum]) {
      acked[acknum] = true;
      new_ACKs++;
      
      while (acked[base]) {
        acked[base] = false;
        base = (base + 1) % SEQSPACE;
      }

      if (base == nextseqnum) {
        if (timer_running) {
          stoptimer(A);
          timer_running = false;
        }
      } else {
        if (timer_running) {
          stoptimer(A);
        }
        starttimer(A, RTT);
        timer_running = true;
      }
    }
  } else {
    if (TRACE > 0)
      printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  int i;
  if (TRACE > 0) printf("----A: time out,resend packets!\n");
  
  for (i = 0; i < WINDOWSIZE; i++) {
    int idx = (base + i) % SEQSPACE;
    if (!acked[idx] && ((nextseqnum + SEQSPACE - base) % SEQSPACE > i)) {
      tolayer3(A, buffer[idx]);
      packets_resent++;
    }
  }
  starttimer(A, RTT);
  timer_running = true;
}



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  int i;
  base = 0;
  nextseqnum = 0;
  timer_running = false;
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
    
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n", seqnum);

    ackpkt.seqnum = seqnum;
    ackpkt.acknum = seqnum;
    for (i = 0; i < 20; i++) 
      ackpkt.payload[i] = '0';
    ackpkt.checksum = ComputeChecksum(ackpkt);

    tolayer3(B, ackpkt);

    if (((seqnum + SEQSPACE - expectedseqnum) % SEQSPACE) < WINDOWSIZE && !received[seqnum]) {
      received[seqnum] = true;
      recv_buffer[seqnum] = packet;

      while (received[expectedseqnum]) {
        tolayer5(B, recv_buffer[expectedseqnum].payload);
        received[expectedseqnum] = false;
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        packets_received++;
      }
    }
  } else {
    if (TRACE > 0)
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
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
