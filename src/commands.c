/*
	commands.c: Commands sent to the card
	Copyright (C) 2003-2010   Ludovic Rousseau
	Copyright (C) 2005 Martin Paljak

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

#include <config.h>

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <pcsclite.h>
#include <ifdhandler.h>
#include <reader.h>

#include "commands.h"
#include "openct/proto-t1.h"
#include "ccid.h"
#include "defs.h"
#include "ccid_ifdhandler.h"
#include "debug.h"
#include "utils.h"

/* The firmware of SCM readers reports dwMaxCCIDMessageLength = 263
 * instead of 270 so this prevents from sending a full length APDU
 * of 260 bytes since the driver check this value */
#define BOGUS_SCM_FIRMWARE_FOR_dwMaxCCIDMessageLength

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define CHECK_STATUS(res) \
	if (STATUS_NO_SUCH_DEVICE == res) \
		return IFD_NO_SUCH_DEVICE; \
	if (STATUS_SUCCESS != res) \
		return IFD_COMMUNICATION_ERROR;

/* internal functions */
static RESPONSECODE CmdXfrBlockTPDU_T0(unsigned int reader_index,
	unsigned int tx_length, unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[]);

static void i2dw(int value, unsigned char *buffer);
static unsigned int bei2i(unsigned char *buffer);


/*****************************************************************************
 *
 *					CmdPowerOn
 *
 ****************************************************************************/
RESPONSECODE CmdPowerOn(unsigned int reader_index, unsigned int * nlength,
	unsigned char buffer[], int voltage)
{
	unsigned char cmd[10];
	unsigned char resp[10 + MAX_ATR_SIZE];
	int bSeq;
	status_t res;
	unsigned int atr_len, length;
	int init_voltage;
	RESPONSECODE return_value = IFD_SUCCESS;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);

	if ((ccid_descriptor->dwFeatures & CCID_CLASS_AUTO_VOLTAGE)
		|| (ccid_descriptor->dwFeatures & CCID_CLASS_AUTO_ACTIVATION))
		voltage = 0;	/* automatic voltage selection */
	else
	{
		int bVoltageSupport = ccid_descriptor->bVoltageSupport;

check_again:
		if ((1 == voltage) && !(bVoltageSupport & 1))
		{
			DEBUG_INFO1("5V requested but not supported by reader");
			voltage = 2;	/* 3V */
		}

		if ((2 == voltage) && !(bVoltageSupport & 2))
		{
			DEBUG_INFO1("3V requested but not supported by reader");
			voltage = 3;	/* 1.8V */
		}

		if ((3 == voltage) && !(bVoltageSupport & 4))
		{
			DEBUG_INFO1("1.8V requested but not supported by reader");
			voltage = 1;	/* 5V */

			/* do not (infinite) loop if bVoltageSupport == 0 */
			if (bVoltageSupport)
				goto check_again;
		}
	}
	init_voltage = voltage;

again:
	bSeq = (*ccid_descriptor->pbSeq)++;
	cmd[0] = 0x62; /* IccPowerOn */
	cmd[1] = cmd[2] = cmd[3] = cmd[4] = 0;	/* dwLength */
	cmd[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd[6] = bSeq;
	cmd[7] = voltage;
	cmd[8] = cmd[9] = 0; /* RFU */

	res = WritePort(reader_index, sizeof(cmd), cmd);
	CHECK_STATUS(res)

	length = sizeof resp;

	res = ReadPort(reader_index, &length, resp, bSeq);
	CHECK_STATUS(res)

	if (length < CCID_RESPONSE_HEADER_SIZE)
	{
		DEBUG_CRITICAL2("Not enough data received: %d bytes", length);
		return IFD_COMMUNICATION_ERROR;
	}

	if (resp[STATUS_OFFSET] & CCID_COMMAND_FAILED)
	{
		ccid_error(PCSC_LOG_ERROR, resp[ERROR_OFFSET], __FILE__, __LINE__, __FUNCTION__);	/* bError */

		/* continue with other voltage values */
		if (voltage)
		{
			const char *voltage_code[] = { "1.8V", "5V", "3V", "1.8V" };

			DEBUG_INFO3("Power up with %s failed. Try with %s.",
				voltage_code[voltage], voltage_code[voltage-1]);
			voltage--;

			/* loop from 5V to 1.8V */
			if (0 == voltage)
				voltage = 3;

			/* continue until we tried every values */
			if (voltage != init_voltage)
				goto again;
		}

		return IFD_COMMUNICATION_ERROR;
	}

	/* extract the ATR */
	atr_len = dw2i(resp, 1);	/* ATR length */
	if (atr_len > *nlength)
		atr_len = *nlength;

	*nlength = atr_len;

	memcpy(buffer, resp+10, atr_len);

	return return_value;
} /* CmdPowerOn */


/*****************************************************************************
 *
 *					SecurePINVerify
 *
 ****************************************************************************/
RESPONSECODE SecurePINVerify(unsigned int reader_index,
	unsigned char TxBuffer[], unsigned int TxLength,
	unsigned char RxBuffer[], unsigned int *RxLength)
{
	unsigned char cmd[11+14+TxLength];
	unsigned int a, b;
	PIN_VERIFY_STRUCTURE *pvs;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int old_read_timeout;
	RESPONSECODE ret;
	status_t res;

	uint32_t ulDataLength;

	pvs = (PIN_VERIFY_STRUCTURE *)TxBuffer;
	cmd[0] = 0x69;	/* Secure */
	cmd[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd[6] = (*ccid_descriptor->pbSeq)++;
	cmd[7] = 0;		/* bBWI */
	cmd[8] = 0;		/* wLevelParameter */
	cmd[9] = 0;
	cmd[10] = 0;	/* bPINOperation: PIN Verification */

	if (TxLength < 19+4 /* 4 = APDU size */)	/* command too short? */
	{
		DEBUG_INFO3("Command too short: %d < %d", TxLength, 19+4);
		return IFD_NOT_SUPPORTED;
	}

	/* On little endian machines we are all set. */
	/* If on big endian machine and caller is using host byte order */
	ulDataLength = get_U32(&pvs->ulDataLength);
	if ((ulDataLength + 19 == TxLength) &&
		(bei2i((unsigned char*)(&pvs->ulDataLength)) == ulDataLength))
	{
		DEBUG_INFO1("Reversing order from big to little endian");
		/* If ulDataLength is big endian, assume others are too */
		/* reverse the byte order for 3 fields */
		p_bswap_16(&pvs->wPINMaxExtraDigit);
		p_bswap_16(&pvs->wLangId);
		p_bswap_32(&pvs->ulDataLength);
	}
	/* At this point we now have the above 3 variables in little endian */

	if (dw2i(TxBuffer, 15) + 19 != TxLength) /* ulDataLength field coherency */
	{
		DEBUG_INFO3("Wrong lengths: %d %d", dw2i(TxBuffer, 15) + 19, TxLength);
		return IFD_NOT_SUPPORTED;
	}

	/* make sure bEntryValidationCondition is valid
	 * The Cherry XX44 reader crashes with a wrong value */
	if ((0x00 == TxBuffer[7]) || (TxBuffer[7] > 0x07))
	{
		DEBUG_INFO2("Fix bEntryValidationCondition (was 0x%02X)",
			TxBuffer[7]);
		TxBuffer[7] = 0x02;
	}

	/* Build a CCID block from a PC/SC V2.02.05 Part 10 block */
	for (a = 11, b = 0; b < TxLength; b++)
	{
		if (1 == b) /* bTimeOut2 field */
			/* Ignore the second timeout as there's nothing we can do with
			 * it currently */
			continue;

		if ((b >= 15) && (b <= 18)) /* ulDataLength field (4 bytes) */
			/* the ulDataLength field is not present in the CCID frame
			 * so do not copy */
			continue;

		/* copy the CCID block 'verbatim' */
		cmd[a] = TxBuffer[b];
		a++;
	}

	i2dw(a - 10, cmd + 1);  /* CCID message length */

	old_read_timeout = ccid_descriptor -> readTimeout;
	ccid_descriptor -> readTimeout = max(90, TxBuffer[0]+10)*1000;	/* at least 90 seconds */

	res = WritePort(reader_index, a, cmd);
	if (STATUS_SUCCESS != res)
	{
		if (STATUS_NO_SUCH_DEVICE == res)
			ret = IFD_NO_SUCH_DEVICE;
		else
			ret = IFD_COMMUNICATION_ERROR;
		goto end;
	}

	ret = CCID_Receive(reader_index, RxLength, RxBuffer, NULL);

end:
	/* Restore initial timeout */
	ccid_descriptor -> readTimeout = old_read_timeout;

	return ret;
} /* SecurePINVerify */

/*****************************************************************************
 *
 *					SecurePINModify
 *
 ****************************************************************************/
RESPONSECODE SecurePINModify(unsigned int reader_index,
	unsigned char TxBuffer[], unsigned int TxLength,
	unsigned char RxBuffer[], unsigned int *RxLength)
{
	unsigned char cmd[11+19+TxLength];
	unsigned int a, b;
	PIN_MODIFY_STRUCTURE *pms;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int old_read_timeout;
	RESPONSECODE ret;
	status_t res;
	uint32_t ulDataLength;

	pms = (PIN_MODIFY_STRUCTURE *)TxBuffer;
	cmd[0] = 0x69;	/* Secure */
	cmd[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd[6] = (*ccid_descriptor->pbSeq)++;
	cmd[7] = 0;		/* bBWI */
	cmd[8] = 0;		/* wLevelParameter */
	cmd[9] = 0;
	cmd[10] = 1;	/* bPINOperation: PIN Modification */

	if (TxLength < 24+4 /* 4 = APDU size */) /* command too short? */
	{
		DEBUG_INFO3("Command too short: %d < %d", TxLength, 24+4);
		return IFD_NOT_SUPPORTED;
	}

	/* On little endian machines we are all set. */
	/* If on big endian machine and caller is using host byte order */
	ulDataLength = get_U32(&pms->ulDataLength);
	if ((ulDataLength + 24 == TxLength) &&
		(bei2i((unsigned char*)(&pms->ulDataLength)) == ulDataLength))
	{
		DEBUG_INFO1("Reversing order from big to little endian");
		/* If ulDataLength is big endian, assume others are too */
		/* reverse the byte order for 3 fields */
		p_bswap_16(&pms->wPINMaxExtraDigit);
		p_bswap_16(&pms->wLangId);
		p_bswap_32(&pms->ulDataLength);
	}
	/* At this point we now have the above 3 variables in little endian */


	if (dw2i(TxBuffer, 20) + 24 != TxLength) /* ulDataLength field coherency */
	{
		DEBUG_INFO3("Wrong lengths: %d %d", dw2i(TxBuffer, 20) + 24, TxLength);
		return IFD_NOT_SUPPORTED;
	}

	/* Make sure in the beginning if bNumberMessage is valid or not.
	 * 0xFF is the default value. */
	if ((TxBuffer[11] > 3) && (TxBuffer[11] != 0xFF))
	{
		DEBUG_INFO2("Wrong bNumberMessage: %d", TxBuffer[11]);
		return IFD_NOT_SUPPORTED;
	}

	/* Make sure bEntryValidationCondition is valid
	 * The Cherry XX44 reader crashes with a wrong value */
	if ((0x00 == TxBuffer[10]) || (TxBuffer[10] > 0x07))
	{
		DEBUG_INFO2("Fix bEntryValidationCondition (was 0x%02X)",
			TxBuffer[10]);
		TxBuffer[10] = 0x02;
	}

	/* Build a CCID block from a PC/SC V2.02.05 Part 10 block */

	/* Do adjustments as needed - CCID spec is not exact with some
	 * details in the format of the structure, per-reader adaptions
	 * might be needed.
	 */
	for (a = 11, b = 0; b < TxLength; b++)
	{
		if (1 == b) /* bTimeOut2 */
			/* Ignore the second timeout as there's nothing we can do with it
			 * currently */
			continue;

		if (15 == b) /* bMsgIndex2 */
		{
			/* in CCID the bMsgIndex2 is present only if bNumberMessage != 0 */
			if (0 == TxBuffer[11])
				continue;
		}

		if (16 == b) /* bMsgIndex3 */
		{
			/* in CCID the bMsgIndex3 is present only if bNumberMessage == 3 */
			if (TxBuffer[11] < 3)
				continue;
		}

		if ((b >= 20) && (b <= 23)) /* ulDataLength field (4 bytes) */
			/* the ulDataLength field is not present in the CCID frame
			 * so do not copy */
			continue;

		/* copy to the CCID block 'verbatim' */
		cmd[a] = TxBuffer[b];
		a++;
	}

	/* We know the size of the CCID message now */
	i2dw(a - 10, cmd + 1);	/* command length (includes bPINOperation) */

	old_read_timeout = ccid_descriptor -> readTimeout;
	ccid_descriptor -> readTimeout = max(90, TxBuffer[0]+10)*1000;	/* at least 90 seconds */

	res = WritePort(reader_index, a, cmd);
	if (STATUS_SUCCESS != res)
	{
		if (STATUS_NO_SUCH_DEVICE == res)
			ret = IFD_NO_SUCH_DEVICE;
		else
			ret = IFD_COMMUNICATION_ERROR;
		goto end;
	}

	ret = CCID_Receive(reader_index, RxLength, RxBuffer, NULL);

end:
	ccid_descriptor -> readTimeout = old_read_timeout;
	return ret;
} /* SecurePINModify */


/*****************************************************************************
 *
 *					Escape
 *
 ****************************************************************************/
RESPONSECODE CmdEscape(unsigned int reader_index,
	const unsigned char TxBuffer[], unsigned int TxLength,
	unsigned char RxBuffer[], unsigned int *RxLength, unsigned int timeout)
{
	return CmdEscapeCheck(reader_index, TxBuffer, TxLength, RxBuffer, RxLength,
		timeout, false);
} /* CmdEscape */


/*****************************************************************************
 *
 *					Escape (with check of gravity)
 *
 ****************************************************************************/
RESPONSECODE CmdEscapeCheck(unsigned int reader_index,
	const unsigned char TxBuffer[], unsigned int TxLength,
	unsigned char RxBuffer[], unsigned int *RxLength, unsigned int timeout,
	bool mayfail)
{
	unsigned char *cmd_in, *cmd_out;
	int bSeq;
	status_t res;
	unsigned int length_in, length_out;
	RESPONSECODE return_value = IFD_SUCCESS;
	int old_read_timeout = -1;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);

	/* a value of 0 do not change the default read timeout */
	if (timeout > 0)
	{
		old_read_timeout = ccid_descriptor -> readTimeout;
		ccid_descriptor -> readTimeout = timeout;
	}

again:
	/* allocate buffers */
	length_in = 10 + TxLength;
	if (NULL == (cmd_in = malloc(length_in)))
	{
		return_value = IFD_COMMUNICATION_ERROR;
		goto end;
	}

	length_out = 10 + *RxLength;
	if (NULL == (cmd_out = malloc(length_out)))
	{
		free(cmd_in);
		return_value = IFD_COMMUNICATION_ERROR;
		goto end;
	}

	bSeq = (*ccid_descriptor->pbSeq)++;
	cmd_in[0] = 0x6B; /* PC_to_RDR_Escape */
	i2dw(length_in - 10, cmd_in+1);	/* dwLength */
	cmd_in[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd_in[6] = bSeq;
	cmd_in[7] = cmd_in[8] = cmd_in[9] = 0; /* RFU */

	/* copy the command */
	memcpy(&cmd_in[10], TxBuffer, TxLength);

	res = WritePort(reader_index, length_in, cmd_in);
	free(cmd_in);
	if (res != STATUS_SUCCESS)
	{
		free(cmd_out);
		if (STATUS_NO_SUCH_DEVICE == res)
			return_value = IFD_NO_SUCH_DEVICE;
		else
			return_value = IFD_COMMUNICATION_ERROR;
		goto end;
	}

time_request:
	length_out = 10 + *RxLength;
	res = ReadPort(reader_index, &length_out, cmd_out, bSeq);

	/* replay the command if NAK
	 * This (generally) happens only for the first command sent to the reader
	 * with the serial protocol so it is not really needed for all the other
	 * ReadPort() calls */
	if (STATUS_COMM_NAK == res)
	{
		free(cmd_out);
		goto again;
	}

	if (res != STATUS_SUCCESS)
	{
		free(cmd_out);
		if (STATUS_NO_SUCH_DEVICE == res)
			return_value = IFD_NO_SUCH_DEVICE;
		else
			return_value = IFD_COMMUNICATION_ERROR;
		goto end;
	}

	if (length_out < CCID_RESPONSE_HEADER_SIZE)
	{
		free(cmd_out);
		DEBUG_CRITICAL2("Not enough data received: %d bytes", length_out);
		return_value = IFD_COMMUNICATION_ERROR;
		goto end;
	}

	if (cmd_out[STATUS_OFFSET] & CCID_TIME_EXTENSION)
	{
		DEBUG_COMM2("Time extension requested: 0x%02X", cmd_out[ERROR_OFFSET]);
		goto time_request;
	}

	if (cmd_out[STATUS_OFFSET] & CCID_COMMAND_FAILED)
	{
		/* mayfail: the error may be expected and not fatal */
		ccid_error(mayfail ? PCSC_LOG_INFO : PCSC_LOG_ERROR,
			cmd_out[ERROR_OFFSET], __FILE__, __LINE__, __FUNCTION__);	/* bError */
		return_value = IFD_COMMUNICATION_ERROR;
	}

	/* copy the response */
	length_out = dw2i(cmd_out, 1);
	if (length_out > *RxLength)
	{
		length_out = *RxLength;
		return_value = IFD_ERROR_INSUFFICIENT_BUFFER;
	}
	*RxLength = length_out;
	memcpy(RxBuffer, &cmd_out[10], length_out);

	free(cmd_out);

end:
	if (timeout > 0)
		ccid_descriptor -> readTimeout = old_read_timeout;

	return return_value;
} /* EscapeCheck */


/*****************************************************************************
 *
 *					CmdPowerOff
 *
 ****************************************************************************/
RESPONSECODE CmdPowerOff(unsigned int reader_index)
{
	unsigned char cmd[10];
	int bSeq;
	status_t res;
	unsigned int length;
	RESPONSECODE return_value = IFD_SUCCESS;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);

	bSeq = (*ccid_descriptor->pbSeq)++;
	cmd[0] = 0x63; /* IccPowerOff */
	cmd[1] = cmd[2] = cmd[3] = cmd[4] = 0;	/* dwLength */
	cmd[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd[6] = bSeq;
	cmd[7] = cmd[8] = cmd[9] = 0; /* RFU */

	res = WritePort(reader_index, sizeof(cmd), cmd);
	CHECK_STATUS(res)

	length = sizeof(cmd);
	res = ReadPort(reader_index, &length, cmd, bSeq);
	CHECK_STATUS(res)

	if (length < CCID_RESPONSE_HEADER_SIZE)
	{
		DEBUG_CRITICAL2("Not enough data received: %d bytes", length);
		return IFD_COMMUNICATION_ERROR;
	}

	if (cmd[STATUS_OFFSET] & CCID_COMMAND_FAILED)
	{
		ccid_error(PCSC_LOG_ERROR, cmd[ERROR_OFFSET], __FILE__, __LINE__, __FUNCTION__);	/* bError */
		return_value = IFD_COMMUNICATION_ERROR;
	}

	return return_value;
} /* CmdPowerOff */


/*****************************************************************************
 *
 *					CmdGetSlotStatus
 *
 ****************************************************************************/
RESPONSECODE CmdGetSlotStatus(unsigned int reader_index, unsigned char buffer[])
{
	unsigned char cmd[10];
	int bSeq;
	status_t res;
	unsigned int length;
	RESPONSECODE return_value = IFD_SUCCESS;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);

	bSeq = (*ccid_descriptor->pbSeq)++;
	cmd[0] = 0x65; /* GetSlotStatus */
	cmd[1] = cmd[2] = cmd[3] = cmd[4] = 0;	/* dwLength */
	cmd[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd[6] = bSeq;
	cmd[7] = cmd[8] = cmd[9] = 0; /* RFU */

	res = WritePort(reader_index, sizeof(cmd), cmd);
	CHECK_STATUS(res)

	length = SIZE_GET_SLOT_STATUS;
	res = ReadPort(reader_index, &length, buffer, bSeq);
	CHECK_STATUS(res)

	if (length < CCID_RESPONSE_HEADER_SIZE)
	{
		DEBUG_CRITICAL2("Not enough data received: %d bytes", length);
		return IFD_COMMUNICATION_ERROR;
	}

	if ((buffer[STATUS_OFFSET] & CCID_COMMAND_FAILED)
		/* card absent or mute is not an communication error */
		&& (buffer[ERROR_OFFSET] != 0xFE))
	{
		return_value = IFD_COMMUNICATION_ERROR;
		ccid_error(PCSC_LOG_ERROR, buffer[ERROR_OFFSET], __FILE__, __LINE__, __FUNCTION__);	/* bError */
	}

	return return_value;
} /* CmdGetSlotStatus */


/*****************************************************************************
 *
 *					CmdXfrBlock
 *
 ****************************************************************************/
RESPONSECODE CmdXfrBlock(unsigned int reader_index, unsigned int tx_length,
	unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[], int protocol)
{
	RESPONSECODE return_value = IFD_SUCCESS;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);

	/* APDU or TPDU? */
	switch (ccid_descriptor->dwFeatures & CCID_CLASS_EXCHANGE_MASK)
	{
		case CCID_CLASS_SHORT_APDU:
			return_value = CmdXfrBlockTPDU_T0(reader_index,
				tx_length, tx_buffer, rx_length, rx_buffer);
			break;

		default:
			return_value = IFD_COMMUNICATION_ERROR;
	}

	return return_value;
} /* CmdXfrBlock */


/*****************************************************************************
 *
 *					CCID_Transmit
 *
 ****************************************************************************/
RESPONSECODE CCID_Transmit(unsigned int reader_index, unsigned int tx_length,
	const unsigned char tx_buffer[], unsigned short rx_length, unsigned char bBWI)
{
	unsigned char cmd[10+tx_length];	/* CCID + APDU buffer */
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	status_t ret;

	cmd[0] = 0x6F; /* XfrBlock */
	i2dw(tx_length, cmd+1);	/* APDU length */
	cmd[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd[6] = (*ccid_descriptor->pbSeq)++;
	cmd[7] = bBWI;	/* extend block waiting timeout */
	cmd[8] = rx_length & 0xFF;	/* Expected length, in character mode only */
	cmd[9] = (rx_length >> 8) & 0xFF;

	if (tx_buffer)
		memcpy(cmd+10, tx_buffer, tx_length);

	ret = WritePort(reader_index, 10+tx_length, cmd);
	CHECK_STATUS(ret)

	return IFD_SUCCESS;
} /* CCID_Transmit */


/*****************************************************************************
 *
 *					CCID_Receive
 *
 ****************************************************************************/
RESPONSECODE CCID_Receive(unsigned int reader_index, unsigned int *rx_length,
	unsigned char rx_buffer[], unsigned char *chain_parameter)
{
	unsigned char cmd[10+CMD_BUF_SIZE];	/* CCID + APDU buffer */
	unsigned int length;
	RESPONSECODE return_value = IFD_SUCCESS;
	status_t ret;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	unsigned int old_timeout;

	/* store the original value of read timeout*/
	old_timeout = ccid_descriptor -> readTimeout;

time_request:
	length = sizeof(cmd);
	ret = ReadPort(reader_index, &length, cmd, -1);

	/* restore the original value of read timeout */
	ccid_descriptor -> readTimeout = old_timeout;
	CHECK_STATUS(ret)

	if (length < CCID_RESPONSE_HEADER_SIZE)
	{
		DEBUG_CRITICAL2("Not enough data received: %d bytes", length);
		return IFD_COMMUNICATION_ERROR;
	}

	if (cmd[STATUS_OFFSET] & CCID_COMMAND_FAILED)
	{
		ccid_error(PCSC_LOG_ERROR, cmd[ERROR_OFFSET], __FILE__, __LINE__, __FUNCTION__);	/* bError */
		switch (cmd[ERROR_OFFSET])
		{
			case 0xEF:	/* cancel */
				if (*rx_length < 2)
					return IFD_ERROR_INSUFFICIENT_BUFFER;
				rx_buffer[0]= 0x64;
				rx_buffer[1]= 0x01;
				*rx_length = 2;
				return IFD_SUCCESS;

			case 0xF0:	/* timeout */
				if (*rx_length < 2)
					return IFD_ERROR_INSUFFICIENT_BUFFER;
				rx_buffer[0]= 0x64;
				rx_buffer[1]= 0x00;
				*rx_length = 2;
				return IFD_SUCCESS;

			case 0xFD:	/* Parity error during exchange */
				return IFD_PARITY_ERROR;

			case 0xFE:	/* Card absent or mute */
				if (2 == (cmd[STATUS_OFFSET] & 0x02)) /* No ICC */
					return IFD_ICC_NOT_PRESENT;
				else
					return IFD_COMMUNICATION_ERROR;

			default:
				return IFD_COMMUNICATION_ERROR;
		}
	}

	if (cmd[STATUS_OFFSET] & CCID_TIME_EXTENSION)
	{
		DEBUG_COMM2("Time extension requested: 0x%02X", cmd[ERROR_OFFSET]);

		/* compute the new value of read timeout */
		if (cmd[ERROR_OFFSET] > 0)
			ccid_descriptor -> readTimeout *= cmd[ERROR_OFFSET];

		DEBUG_COMM2("New timeout: %d ms", ccid_descriptor -> readTimeout);
		goto time_request;
	}

	/* we have read less (or more) data than the CCID frame says to contain */
	if (length-10 != dw2i(cmd, 1))
	{
		DEBUG_CRITICAL3("Can't read all data (%d out of %d expected)",
			length-10, dw2i(cmd, 1));
		return_value = IFD_COMMUNICATION_ERROR;
	}

	length = dw2i(cmd, 1);
	if (length <= *rx_length)
		*rx_length = length;
	else
	{
		DEBUG_CRITICAL2("overrun by %d bytes", length - *rx_length);
		length = *rx_length;
		return_value = IFD_ERROR_INSUFFICIENT_BUFFER;
	}

	/* Kobil firmware bug. No support for chaining */
	if (length && (NULL == rx_buffer))
	{
		DEBUG_CRITICAL2("Nul block expected but got %d bytes", length);
		return_value = IFD_COMMUNICATION_ERROR;
	}
	else
		if (length)
			memcpy(rx_buffer, cmd+10, length);

	/* Extended case?
	 * Only valid for RDR_to_PC_DataBlock frames */
	if (chain_parameter)
		*chain_parameter = cmd[CHAIN_PARAMETER_OFFSET];

	return return_value;
} /* CCID_Receive */

/*****************************************************************************
 *
 *					CmdXfrBlockTPDU_T0
 *
 ****************************************************************************/
static RESPONSECODE CmdXfrBlockTPDU_T0(unsigned int reader_index,
	unsigned int tx_length, unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[])
{
	RESPONSECODE return_value = IFD_SUCCESS;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);

	DEBUG_COMM2("T=0: %d bytes", tx_length);

	/* command length too big for CCID reader? */
	if (tx_length > ccid_descriptor->dwMaxCCIDMessageLength-10)
	{
#ifdef BOGUS_SCM_FIRMWARE_FOR_dwMaxCCIDMessageLength
		if (263 == ccid_descriptor->dwMaxCCIDMessageLength)
		{
			DEBUG_INFO3("Command too long (%d bytes) for max: %d bytes."
				" SCM reader with bogus firmware?",
				tx_length, ccid_descriptor->dwMaxCCIDMessageLength-10);
		}
		else
#endif
		{
			DEBUG_CRITICAL3("Command too long (%d bytes) for max: %d bytes",
				tx_length, ccid_descriptor->dwMaxCCIDMessageLength-10);
			return IFD_COMMUNICATION_ERROR;
		}
	}

	/* command length too big for CCID driver? */
	if (tx_length > CMD_BUF_SIZE)
	{
		DEBUG_CRITICAL3("Command too long (%d bytes) for max: %d bytes",
				tx_length, CMD_BUF_SIZE);
		return IFD_COMMUNICATION_ERROR;
	}

	return_value = CCID_Transmit(reader_index, tx_length, tx_buffer, 0, 0);
	if (return_value != IFD_SUCCESS)
		return return_value;

	return CCID_Receive(reader_index, rx_length, rx_buffer, NULL);
} /* CmdXfrBlockTPDU_T0 */

/*****************************************************************************
 *
 *					SetParameters
 *
 ****************************************************************************/
RESPONSECODE SetParameters(unsigned int reader_index, char protocol,
	unsigned int length, unsigned char buffer[])
{
	unsigned char cmd[10+length];	/* CCID + APDU buffer */
	int bSeq;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	status_t res;

	DEBUG_COMM2("length: %d bytes", length);

	bSeq = (*ccid_descriptor->pbSeq)++;
	cmd[0] = 0x61; /* SetParameters */
	i2dw(length, cmd+1);	/* APDU length */
	cmd[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd[6] = bSeq;
	cmd[7] = protocol;	/* bProtocolNum */
	cmd[8] = cmd[9] = 0; /* RFU */

	memcpy(cmd+10, buffer, length);

	res = WritePort(reader_index, 10+length, cmd);
	CHECK_STATUS(res)

	length = sizeof(cmd);
	res = ReadPort(reader_index, &length, cmd, bSeq);
	CHECK_STATUS(res)

	if (length < CCID_RESPONSE_HEADER_SIZE)
	{
		DEBUG_CRITICAL2("Not enough data received: %d bytes", length);
		return IFD_COMMUNICATION_ERROR;
	}

	if (cmd[STATUS_OFFSET] & CCID_COMMAND_FAILED)
	{
		ccid_error(PCSC_LOG_ERROR, cmd[ERROR_OFFSET], __FILE__, __LINE__, __FUNCTION__);	/* bError */
		if (0x00 == cmd[ERROR_OFFSET])	/* command not supported */
			return IFD_NOT_SUPPORTED;
		else
			if ((cmd[ERROR_OFFSET] >= 1) && (cmd[ERROR_OFFSET] <= 127))
				/* a parameter is not changeable */
				return IFD_SUCCESS;
			else
				return IFD_COMMUNICATION_ERROR;
	}

	return IFD_SUCCESS;
} /* SetParameters */


/*****************************************************************************
 *
 *					i2dw
 *
 ****************************************************************************/
static void i2dw(int value, unsigned char buffer[])
{
	buffer[0] = value & 0xFF;
	buffer[1] = (value >> 8) & 0xFF;
	buffer[2] = (value >> 16) & 0xFF;
	buffer[3] = (value >> 24) & 0xFF;
} /* i2dw */

/*****************************************************************************
*
*					bei2i (big endian integer to host order integer)
*
****************************************************************************/

static unsigned int bei2i(unsigned char buffer[])
{
	return (buffer[0]<<24) + (buffer[1]<<16) + (buffer[2]<<8) + buffer[3];
}
