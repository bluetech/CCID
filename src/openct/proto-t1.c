/*
 * Implementation of T=1
 *
 * Copyright (C) 2003, Olaf Kirch <okir@suse.de>
 *
 * improvements by:
 * Copyright (C) 2004 Ludovic Rousseau <ludovic.rousseau@free.fr>
 */

#include <config.h>
#include <stdbool.h>

#include <pcsclite.h>
#include <ifdhandler.h>
#include "commands.h"
#include "debug.h"
#include "proto-t1.h"

#include "ccid.h"

/* I block */
#define T1_I_SEQ_SHIFT		6

/* R block */
#define T1_IS_ERROR(pcb)	((pcb) & 0x0F)
#define T1_EDC_ERROR		0x01
#define T1_OTHER_ERROR		0x02
#define T1_R_SEQ_SHIFT		4

/* S block stuff */
#define T1_S_IS_RESPONSE(pcb)	((pcb) & T1_S_RESPONSE)
#define T1_S_TYPE(pcb)		((pcb) & 0x0F)
#define T1_S_RESPONSE		0x20
#define T1_S_RESYNC		0x00
#define T1_S_IFS		0x01
#define T1_S_ABORT		0x02
#define T1_S_WTX		0x03

#define swap_nibbles(x) ( (x >> 4) | ((x & 0xF) << 4) )

#define NAD 0
#define PCB 1
#define LEN 2
#define DATA 3

/* internal state, do not mess with it. */
/* should be != DEAD after reset/init */
enum {
	SENDING, RECEIVING, RESYNCH, DEAD
};

static void t1_set_checksum(t1_state_t *, int);

/*
 * Set default T=1 protocol parameters
 */
static void t1_set_defaults(t1_state_t * t1)
{
	/* This timeout is rather insane, but we need this right now
	 * to support cryptoflex keygen */
	t1->ifsc = 32;
	t1->ifsd = 32;
}

static void t1_set_checksum(t1_state_t * t1, int csum)
{
	switch (csum) {
	case IFD_PROTOCOL_T1_CHECKSUM_LRC:
		t1->rc_bytes = 1;
		break;
	case IFD_PROTOCOL_T1_CHECKSUM_CRC:
		t1->rc_bytes = 2;
		break;
	}
}

/*
 * Attach t1 protocol
 */
int t1_init(t1_state_t * t1, int lun)
{
	t1_set_defaults(t1);
	t1_set_param(t1, IFD_PROTOCOL_T1_CHECKSUM_LRC, 0);
	t1_set_param(t1, IFD_PROTOCOL_T1_STATE, SENDING);
	t1_set_param(t1, IFD_PROTOCOL_T1_MORE, false);
	t1_set_param(t1, IFD_PROTOCOL_T1_NAD, 0);

	t1->lun = lun;

	return 0;
}

/*
 * Detach t1 protocol
 */
void t1_release(/*@unused@*/ t1_state_t * t1)
{
	(void)t1;
	/* NOP */
}

/*
 * Set parameters for T1 protocol
 */
int t1_set_param(t1_state_t * t1, int type, long value)
{
	switch (type) {
	case IFD_PROTOCOL_T1_CHECKSUM_LRC:
	case IFD_PROTOCOL_T1_CHECKSUM_CRC:
		t1_set_checksum(t1, type);
		break;
	case IFD_PROTOCOL_T1_IFSC:
		t1->ifsc = value;
		break;
	case IFD_PROTOCOL_T1_IFSD:
		t1->ifsd = value;
		break;
	case IFD_PROTOCOL_T1_STATE:
		t1->state = value;
		break;
	case IFD_PROTOCOL_T1_MORE:
		t1->more = value;
		break;
	case IFD_PROTOCOL_T1_NAD:
		t1->nad = value;
		break;
	default:
		DEBUG_INFO2("Unsupported parameter %d", type);
		return -1;
	}

	return 0;
}

