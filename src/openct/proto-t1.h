/*
    proto-t1.h: header file for proto-t1.c
    Copyright (C) 2004   Ludovic Rousseau

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with this library; if not, write to the Free Software Foundation,
	Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef __PROTO_T1_H__
#define __PROTO_T1_H__

#include <config.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* T=1 protocol constants */
#define T1_I_BLOCK		0x00
#define T1_R_BLOCK		0x80
#define T1_S_BLOCK		0xC0
#define T1_MORE_BLOCKS		0x20

enum {
	IFD_PROTOCOL_T1_BLOCKSIZE,
	IFD_PROTOCOL_T1_CHECKSUM_CRC,
	IFD_PROTOCOL_T1_CHECKSUM_LRC,
	IFD_PROTOCOL_T1_IFSC,
	IFD_PROTOCOL_T1_IFSD,
	IFD_PROTOCOL_T1_STATE,
	IFD_PROTOCOL_T1_MORE,
	IFD_PROTOCOL_T1_NAD
};

/* see /usr/include/PCSC/ifdhandler.h for other values
 * this one is for internal use only */
#define IFD_PARITY_ERROR 699

typedef struct {
	int		lun;
	int		state;

	unsigned int	ifsc;
	unsigned int	ifsd;

	unsigned int	nad;

	unsigned char	wtx;
	unsigned int	rc_bytes;

	bool			more;	/* more data bit */
	unsigned char	previous_block[4];	/* to store the last R-block */
} t1_state_t;

int t1_init(t1_state_t *t1, int lun);
void t1_release(t1_state_t *t1);
int t1_set_param(t1_state_t *t1, int type, long value);

#endif

