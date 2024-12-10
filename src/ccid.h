/*
    ccid.h: CCID structures
    Copyright (C) 2003-2010   Ludovic Rousseau

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

#include <stdbool.h>

typedef struct
{
	/*
	 * CCID Sequence number
	 */
	unsigned char *pbSeq;
	unsigned char real_bSeq;

	/*
	 * VendorID << 16 + ProductID
	 */
	int readerID;

	/*
	 * Maximum message length
	 */
	unsigned int dwMaxCCIDMessageLength;

	/*
	 * Maximum IFSD
	 */
	int dwMaxIFSD;

	/*
	 * Features supported by the reader (directly from Class Descriptor)
	 */
	int dwFeatures;

	/*
	 * PIN support of the reader (directly from Class Descriptor)
	 */
	char bPINSupport;

	/*
	 * Display dimensions of the reader (directly from Class Descriptor)
	 */
	unsigned int wLcdLayout;

	/*
	 * Default Clock
	 */
	int dwDefaultClock;

	/*
	 * Max Data Rate
	 */
	unsigned int dwMaxDataRate;

	/*
	 * Number of available slots
	 */
	char bMaxSlotIndex;

	/*
	 * Maximum number of slots which can be simultaneously busy
	 */
	char bMaxCCIDBusySlots;

	/*
	 * Slot in use
	 */
	char bCurrentSlotIndex;

	/*
	 * The array of data rates supported by the reader
	 */
	unsigned int *arrayOfSupportedDataRates;

	/*
	 * Read communication port timeout
	 * value is milliseconds
	 * this value can evolve dynamically if card request it (time processing).
	 */
	unsigned int readTimeout;

	/*
	 * Card protocol
	 */
	int cardProtocol;

	/*
	 * Reader protocols
	 */
	int dwProtocols;

	/*
	 * bInterfaceProtocol (CCID, ICCD-A, ICCD-B)
	 */
	int bInterfaceProtocol;

	/*
	 * bNumEndpoints
	 */
	int bNumEndpoints;

	/*
	 * bVoltageSupport (bit field)
	 * 1 = 5.0V
	 * 2 = 3.0V
	 * 4 = 1.8V
	 */
	int bVoltageSupport;

	/*
	 * USB serial number of the device (if any)
	 */
	char *sIFD_serial_number;

	/*
	 * USB iManufacturer string
	 */
	char *sIFD_iManufacturer;

	/*
	 * USB bcdDevice
	 */
	int IFD_bcdDevice;
} _ccid_descriptor;

/* Features from dwFeatures */
#define CCID_CLASS_AUTO_CONF_ATR	0x00000002
#define CCID_CLASS_AUTO_ACTIVATION	0x00000004
#define CCID_CLASS_AUTO_VOLTAGE		0x00000008
#define CCID_CLASS_AUTO_BAUD		0x00000020
#define CCID_CLASS_AUTO_PPS_PROP	0x00000040
#define CCID_CLASS_AUTO_PPS_CUR		0x00000080
#define CCID_CLASS_AUTO_IFSD		0x00000400
#define CCID_CLASS_CHARACTER		0x00000000
#define CCID_CLASS_TPDU				0x00010000
#define CCID_CLASS_SHORT_APDU		0x00020000
#define CCID_CLASS_EXTENDED_APDU	0x00040000
#define CCID_CLASS_EXCHANGE_MASK	0x00070000

/* Features from bPINSupport */
#define CCID_CLASS_PIN_VERIFY		0x01
#define CCID_CLASS_PIN_MODIFY		0x02

/* See CCID specs ch. 4.2.1 */
#define CCID_ICC_PRESENT_ACTIVE		0x00	/* 00 0000 00 */
#define CCID_ICC_PRESENT_INACTIVE	0x01	/* 00 0000 01 */
#define CCID_ICC_ABSENT				0x02	/* 00 0000 10 */
#define CCID_ICC_STATUS_MASK		0x03	/* 00 0000 11 */

#define CCID_COMMAND_FAILED			0x40	/* 01 0000 00 */
#define CCID_TIME_EXTENSION			0x80	/* 10 0000 00 */

/* bInterfaceProtocol for ICCD */
#define PROTOCOL_CCID	0	/* plain CCID */

#define GET_VENDOR(readerID) ((readerID >> 16) & 0xFFFF)

/*
 * Possible values :
 * 3 -> 1.8V, 3V, 5V
 * 2 -> 3V, 5V, 1.8V
 * 1 -> 5V, 1.8V, 3V
 * 0 -> automatic (selection made by the reader)
 */
/*
 * The default is to start at 5V
 * otherwise we would have to parse the ATR and get the value of TAi (i>2) when
 * in T=15
 */
#define VOLTAGE_AUTO 0
#define VOLTAGE_5V 1
#define VOLTAGE_3V 2
#define VOLTAGE_1_8V 3

int ccid_open_hack_pre(unsigned int reader_index);
void ccid_error(int log_level, int error, const char *file, int line,
	const char *function);
_ccid_descriptor *get_ccid_descriptor(unsigned int reader_index);

/* convert a 4 byte integer in USB format into an int */
#define dw2i(a, x) (unsigned int)(((((((unsigned int)a[x+3] << 8) + (unsigned int)a[x+2]) << 8) + (unsigned int)a[x+1]) << 8) + (unsigned int)a[x])
