/*
    ccid_usb.h:  USB access routines using the libusb library
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

#ifndef __CCID_USB_H__
#define __CCID_USB_H__
status_t OpenUSB(unsigned int reader_index, int channel);

status_t OpenUSBByName(unsigned int reader_index, /*@null@*/ char *device);

status_t WriteUSB(unsigned int reader_index, unsigned int length,
	unsigned char *Buffer);

status_t ReadUSB(unsigned int reader_index, unsigned int *length,
	/*@out@*/ unsigned char *Buffer, int bSeq);

status_t CloseUSB(unsigned int reader_index);
status_t DisconnectUSB(unsigned int reader_index);

#include <libusb.h>
/*@null@*/ const struct libusb_interface *get_ccid_usb_interface(
	struct libusb_config_descriptor *desc, int *num);

const unsigned char *get_ccid_device_descriptor(const struct libusb_interface *usb_interface);

uint8_t get_ccid_usb_bus_number(int reader_index);
uint8_t get_ccid_usb_device_address(int reader_index);

int ControlUSB(int reader_index, int requesttype, int request, int value,
	unsigned char *bytes, unsigned int size);

/* A notification received from the CCID. */
struct notification {
    /*
     * The notification type.
     * Standard CCID notifications:
     * - RDR_to_PC_NotifySlotChange
     * - RDR_to_PC_HardwareError
     * On timeout/error, set to 0x00.
     */
    unsigned char messageType;
    /*
     * If RDR_to_PC_NotifySlotChange, the bmSlotICCState value *for the specific
     * slot*:
     * Bit 0: Slot current state - 0b = no ICC present, 1b = ICC present.
     * Bit 1: Slot changed status - 0b = no change, 1b = change.
     */
    unsigned char slotICCState;
};

int InterruptRead(int reader_index, int timeout /* in ms */, struct notification *out);
void InterruptStop(int reader_index);
#endif
