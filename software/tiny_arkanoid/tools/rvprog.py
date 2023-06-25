#!/usr/bin/env python3
# ===================================================================================
# Project:   rvprog - Programming Tool for WCH-LinkE and CH32Vxxx
# Version:   v1.2
# Year:      2023
# Author:    Stefan Wagner
# Github:    https://github.com/wagiminator
# License:   MIT License
# ===================================================================================
#
# Description:
# ------------
# Simple Python tool for flashing CH32Vxxx microcontrollers using the WCH-LinkE or
# compatible programmers/debuggers. The code is based on CNLohr's minichlink.
# Currently supports: CH32V003, CH32V203, CH32V208, CH32V303, CH32V305, CH32V307.
#
# References:
# -----------
# - CNLohr minichlink: https://github.com/cnlohr/ch32v003fun/tree/master/minichlink
# - My mighty USB sniffer
#
# Dependencies:
# -------------
# - PyUSB
#
# Operating Instructions:
# -----------------------
# You need to install PyUSB to use rvprog. Install it via "python -m pip install pyusb".
#
# Linux users need permission to access the device. Run:
# echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8010", MODE="666"' | sudo tee /etc/udev/rules.d/99-WCH-LinkE.rules
# echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8012", MODE="666"' | sudo tee -a /etc/udev/rules.d/99-WCH-LinkE.rules
# sudo udevadm control --reload-rules
#
# Connect the WCH-LinkE to your PC and to your CH32Vxxx board. The WCH-LinkE must be 
# in LinkRV mode (blue LED off)! If not, run: python rvprog.py -v
# Run:
# - python rvprog.py [-h] [-a] [-v] [-b] [-u] [-l] [-e] [-G] [-R] [-f FLASH]
#   -h, --help                show help message and exit
#   -a, --armmode             switch WCH-Link to ARM mode
#   -v, --rvmode              switch WCH-Link to RISC-V mode
#   -b, --unbrick             unbrick chip (CH32V003 only)
#   -u, --unlock              unlock chip (remove read protection)
#   -l, --lock                lock chip (set read protection)
#   -e, --erase               perform a whole chip erase
#   -G, --pingpio             make nRST pin a GPIO pin (CH32V003 only)
#   -R, --pinreset            make nRST pin a reset pin (CH32V003 only)
#   -f FLASH, --flash FLASH   write BIN file to flash
#
# - Example:
#   python rvprog.py -f firmware.bin


import usb.core
import usb.util
import sys
import time
import argparse

# ===================================================================================
# Main Function
# ===================================================================================

def _main():
    # Parse command line arguments
    parser = argparse.ArgumentParser(description='Minimal command line interface for CH32Vxxx programming')
    parser.add_argument('-a', '--armmode',  action='store_true', help='switch WCH-Link to ARM mode')
    parser.add_argument('-v', '--rvmode',   action='store_true', help='switch WCH-Link to RISC-V mode')
    parser.add_argument('-b', '--unbrick',  action='store_true', help='unbrick chip (CH32V003 only)')
    parser.add_argument('-u', '--unlock',   action='store_true', help='unlock chip (remove read protection)')
    parser.add_argument('-l', '--lock',     action='store_true', help='lock chip (set read protection)')
    parser.add_argument('-e', '--erase',    action='store_true', help='perform a whole chip erase')
    parser.add_argument('-G', '--pingpio',  action='store_true', help='make nRST pin a GPIO pin (CH32V003 only)')
    parser.add_argument('-R', '--pinreset', action='store_true', help='make nRST pin a reset pin (CH32V003 only)')
    parser.add_argument('-f', '--flash',    help='write BIN file to flash and verify')
    args = parser.parse_args(sys.argv[1:])

    # Check arguments
    if not any( (args.armmode, args.rvmode, args.unbrick, args.unlock, args.lock, args.erase, args.pingpio, args.pinreset, args.flash) ):
        print('No arguments - no action!')
        sys.exit(0)

    # Switch WCH-Link to RISC-V mode
    try:
        if args.rvmode:
            print('Searching for WCH-Link in ARM mode ...')
            armlink = usb.core.find(idVendor = CH_VENDOR_ID, idProduct = 0x8012)
            if armlink is None:
                raise Exception('No WCH-Link in ARM mode found!')
            print('SUCCESS: Found WCH-Link in ARM mode.')
            print('Switching WCH-Link to RISC-V mode ...')
            armlink.write(0x02, b'\x81\xff\x01\x52')
            time.sleep(2)
            print('DONE.')

    except Exception as ex:
        sys.stderr.write('ERROR: ' + str(ex) + '!\n')

    # Establish connection to WCH-Link
    try:
        print('Searching for WCH-Link in RISC-V mode ...')
        isp = Programmer()
        print('SUCCESS: Found', isp.linkname, 'v' + isp.linkversion + ' in RISC-V mode.')
    except Exception as ex:
        sys.stderr.write('ERROR: ' + str(ex) + '!\n')
        sys.exit(1)

    # Establish connection to target MCU
    try:
        if any( (args.unbrick, args.unlock, args.lock, args.erase, args.pingpio, args.pinreset, args.flash) ):
            print('Connecting to MCU ...')
            isp.connect()
            print('SUCCESS: Connected to', isp.chipname, 'with', isp.flashsize, 'bytes of flash.')
    except Exception as ex:
        sys.stderr.write('ERROR: ' + str(ex) + '!\n')
        isp.exit()
        sys.exit(1)

    # Performing actions
    try:
        # Unbrick chip
        if args.unbrick:
            print('Unbricking chip ...')
            isp.unbrick()
            print('SUCCESS: Chip is unbricked.')

        # Unlock chip
        if args.unlock:
            print('Unlocking chip ...')
            isp.unlock()
            print('SUCCESS: Chip is unlocked.')

        # Perform chip erase
        if args.erase:
            print('Performing whole chip erase ...')
            isp.erase()
            print('SUCCESS: Chip is erased.')

        # Flash binary file
        if args.flash is not None:
            print('Flashing', args.flash, 'to', isp.chipname, '...')
            with open(args.flash, 'rb') as f: data = f.read()
            isp.flash_data(data)
            print('SUCCESS:', len(data), 'bytes written and verified.')

        # Make nRST pin a normal GPIO pin
        if args.pingpio:
            print('Configuring nRST pin as GPIO ...')
            isp.setnrstasgpio(1)
            print('SUCCESS: nRST pin is now a GPIO pin.')

        # Make nRST pin a reset pin
        if args.pinreset:
            print('Configuring nRST pin as reset ...')
            isp.setnrstasgpio(0)
            print('SUCCESS: nRST pin is now a reset pin.')

        # Lock chip
        if args.lock:
            print('Locking chip ...')
            isp.lock()
            print('SUCCESS: Chip is locked.')

        # Switch device to ARM mode
        if args.armmode:
            print('Switching WCH-Link to ARM mode ...')
            isp.exit()
            isp.dev.write(CH_EP_OUT, b'\x81\xff\x01\x41')
            print('DONE: Check if blue LED lights up!')
            sys.exit(0)

        isp.exit()

    except Exception as ex:
        sys.stderr.write('ERROR: ' + str(ex) + '!\n')
        isp.exit()
        sys.exit(1)

    print('DONE.')
    sys.exit(0)

# ===================================================================================
# Programmer Class
# ===================================================================================

class Programmer:
    # Init programmer
    def __init__(self):
        # Find programmer
        self.dev = usb.core.find(idVendor = CH_VENDOR_ID, idProduct = CH_PRODUCT_ID)
        if self.dev is None:
            raise Exception('WCH-Link not found. Check if device is in RISC-V mode')

        # Clear receive buffers
        self.clearreply()

        # Get programmer info
        reply = self.sendcommand(b'\x81\x0d\x01\x01')
        if reply[5] == 1:
            self.linkname = 'CH549-based WCH-Link'
        elif reply[5] == 2:
            self.linkname = 'CH32V307-based WCH-Link'
        elif reply[5] == 3:
            self.linkname = 'CH32V203-based WCH-Link'
        elif reply[5] == 4:
            self.linkname = 'WCH-LinkB'
        elif reply[5] == 18:
            self.linkname = 'WCH-LinkE'
        else:
            raise Exception('Unknown programmer')
        self.linkdevice = reply[5]
        self.linkversion = str(reply[3]) + '.' + str(reply[4])

    # Connect programmer to MCU
    def connect(self):
        # Connect to target MCU and get type
        success = 0
        for _ in range(3):
            reply = self.sendcommand(b'\x81\x0d\x01\x02')
            if len(reply) < 8 or set(reply[:4]) == set((0x81, 0x55, 0x01, 0x01)):
                time.sleep(0.2)
                continue
            self.chipseries =  reply[4]<<4
            self.chiptype   = (reply[4]<<4) + (reply[5]>>4)
            self.chipname   = 'CH32V%03x' % self.chiptype
            if (self.chiptype == 0xfff):
                time.sleep(0.2)
                continue
            success = 1
            break
        if success == 0:
            raise Exception('Failed to connect to target MCU')

        # Read some chip data
        if self.chipseries == 0x000:
            reply = self.sendcommand(b'\x81\x11\x01\x09')
        else:
            reply = self.sendcommand(b'\x81\x11\x01\x05')
        self.flashsize = int.from_bytes(reply[2:4], byteorder='big') * 1024

    # Send command to programmer
    def sendcommand(self, stream):
        self.dev.write(CH_EP_OUT, stream)
        return self.dev.read(CH_EP_IN, CH_PACKET_SIZE, CH_TIMEOUT)

    # Clear USB receive buffers
    def clearreply(self):
        try:
            self.dev.read(CH_EP_IN, CH_PACKET_SIZE, 1)
            self.dev.read(CH_EP_IN_RAW, CH_PACKET_SIZE, 1)
        except:
            None

    # Write to MCU register
    def writereg(self, addr, data):
        stream = bytes((0x81, 0x08, 0x06, addr)) + data.to_bytes(4, byteorder='big') + b'\x02'
        reply = self.sendcommand(stream)
        if (len(reply) != 9) or (reply[3] != addr):
            raise Exception('Failed to write register')

    # Read from MCU register
    def readreg(self, addr):
        stream = (0x81, 0x08, 0x06, addr, 0, 0, 0, 0, 1)
        reply = self.sendcommand(stream)
        if (len(reply) != 9) or (reply[3] != addr):
            raise Exception('Failed to read register')
        return int.from_bytes(reply[4:8], byteorder='big')

    # Unbrick MCU
    def unbrick(self):
        if self.chipseries == 0x000:
            self.sendcommand(b'\x81\x0d\x01\x0f\x09')
        else:
            raise Exception('Unbrick not available for this MCU')

    # Lock MCU (set read protection)
    def lock(self):
        if self.chipseries == 0x000:
            self.sendcommand(b'\x81\x06\x08\x03\xf7\xff\xff\xff\xff\xff\xff')
        else:
            self.sendcommand(b'\x81\x06\x08\x03\x3f\xff\xff\xff\xff\xff\xff')

    # Unlock MCU (remove read protection -> erase chip!!!)
    def unlock(self):
        reply = self.sendcommand(b'\x81\x06\x01\x01')
        if reply[3] == 0x01:
            if self.chipseries == 0x000:
                self.sendcommand(b'\x81\x06\x08\x02\xf7\xff\xff\xff\xff\xff\xff')
            else:
                self.sendcommand(b'\x81\x06\x08\x02\x3f\xff\xff\xff\xff\xff\xff')

    # Perform a whole chip erase
    def erase(self):
        self.sendcommand(b'\x81\x02\x01\x01')
        self.sendcommand(b'\x81\x0d\x01\x02')

    # Make reset pin a normal GPIO pin (0=RESET, 1=GPIO)
    def setnrstasgpio(self, state):
        if self.chipseries == 0x000:
            if state:
                self.sendcommand(b'\x81\x06\x08\x02\xff\xff\xff\xff\xff\xff\xff')
            else:
                self.sendcommand(b'\x81\x06\x08\x02\xf7\xff\xff\xff\xff\xff\xff')
        else:
            raise Exception('RST pin option not available for this MCU')

    # Enable/disable programmer's 3V3 output
    def poweron3v3(self):
        self.sendcommand(b'\x81\x0d\x01\x09')
    def poweroff3v3(self):
        self.sendcommand(b'\x81\x0d\x01\x0a')

    # Enable/disable programmer's 5V output
    def poweron5v(self):
        self.sendcommand(b'\x81\x0d\x01\x0b')
    def poweroff5v(self):
        self.sendcommand(b'\x81\x0d\x01\x0c')

    # Disconnect from MCU
    def exit(self):
        self.sendcommand(b'\x81\x0b\x01\x01')
        self.sendcommand(b'\x81\x0d\x01\xff')

    #--------------------------------------------------------------

    # Get padded data length
    def padlen(self, data, pagesize):
        if (len(data) % pagesize) == 0:
            return len(data)
        else:
            return (len(data) + (pagesize - (len(data) % pagesize)))

    # Pad data and divide into pages
    def page_data(self, data, pagesize):
        if (len(data) % pagesize) > 0:
            data += b'\xff' * (pagesize - (len(data) % pagesize))
        total_length = len(data)
        result = list()
        while len(result) < total_length / pagesize:
            result.append(data[:pagesize])
            data = data[pagesize:]
        return result

    # Write data blob to flash, 64-byte aligned (CH32V003)
    def writebinaryblob003(self, addr, data):
        if addr & 63:
            raise Exception('Blob is not 64-byte aligned')
        self.unlock()
        stream = b'\x81\x01\x08' \
               + addr.to_bytes(4, byteorder='big') \
               + self.padlen(data, 64).to_bytes(4, byteorder='big')
        self.sendcommand(stream)
        self.sendcommand(b'\x81\x02\x01\x05')
        pages = self.page_data(BOOTLOADER003, 128)
        for page in pages:
            self.dev.write(CH_EP_OUT_RAW, page)
        self.sendcommand(b'\x81\x02\x01\x07')
        self.sendcommand(b'\x81\x02\x01\x04')
        pages = self.page_data(data, 64)
        for page in pages:
            self.dev.write(CH_EP_OUT_RAW, page)
        reply = self.dev.read(CH_EP_IN_RAW, CH_PACKET_SIZE, CH_TIMEOUT)
        if set(reply) != set((0x41, 0x01, 0x01, 0x04)):
            raise Exception('Failed writing/verifying data blob')

    # Write data blob to flash, 256-byte aligned (CH32V20x/30x)
    def writebinaryblob203(self, addr, data):
        if addr & 255:
            raise Exception('Blob is not 256-byte aligned')
        self.unlock()
        stream = b'\x81\x01\x08' \
               + addr.to_bytes(4, byteorder='big') \
               + self.padlen(data, 256).to_bytes(4, byteorder='big')
        self.sendcommand(stream)
        self.sendcommand(b'\x81\x02\x01\x05')
        pages = self.page_data(BOOTLOADER203, 128)
        for page in pages:
            self.dev.write(CH_EP_OUT_RAW, page)
        self.sendcommand(b'\x81\x02\x01\x07')
        self.sendcommand(b'\x81\x02\x01\x04')
        pages = self.page_data(data, 256)
        for page in pages:
            self.dev.write(CH_EP_OUT_RAW, page)
        reply = self.dev.read(CH_EP_IN_RAW, CH_PACKET_SIZE, CH_TIMEOUT)
        if set(reply) != set((0x41, 0x01, 0x01, 0x04)):
           raise Exception('Failed writing/verifying data blob')

    # Write data to code flash
    def flash_data(self, data):
        if len(data) > self.flashsize:
            raise Exception('Not enough memory')
        if self.chipseries == 0x000:
            self.writebinaryblob003(CH_CODE_BASE, data)
        else:
            self.writebinaryblob203(CH_CODE_BASE, data)

# ===================================================================================
# Debug Protocol Constants
# ===================================================================================

# USB device settings
CH_VENDOR_ID    = 0x1A86    # VID
CH_PRODUCT_ID   = 0x8010    # PID
CH_PACKET_SIZE  = 1024      # packet size
CH_INTERFACE    = 0         # interface number
CH_EP_OUT       = 0x01      # endpoint for command transfer out
CH_EP_IN        = 0x81      # endpoint for reply transfer in
CH_EP_OUT_RAW   = 0x02      # endpoint for raw data transfer out
CH_EP_IN_RAW    = 0x82      # endpoint for raw data transfer in
CH_TIMEOUT      = 5000      # timeout for USB operations

# Memory constants
CH_RAM_BASE     = 0x20000000
CH_CODE_BASE    = 0x08000000
CB_BOOT_BASE    = 0x1FFFF000

# ===================================================================================
# Flash Bootloader
# ===================================================================================

BOOTLOADER003 = \
     b"\x21\x11\x22\xca\x26\xc8\x93\x77\x15\x00\x99\xcf\xb7\x06\x67\x45" \
    +b"\xb7\x27\x02\x40\x93\x86\x36\x12\x37\x97\xef\xcd\xd4\xc3\x13\x07" \
    +b"\xb7\x9a\xd8\xc3\xd4\xd3\xd8\xd3\x93\x77\x25\x00\x9d\xc7\xb7\x27" \
    +b"\x02\x40\x98\x4b\xad\x66\x37\x33\x00\x40\x13\x67\x47\x00\x98\xcb" \
    +b"\x98\x4b\x93\x86\xa6\xaa\x13\x67\x07\x04\x98\xcb\xd8\x47\x05\x8b" \
    +b"\x63\x16\x07\x10\x98\x4b\x6d\x9b\x98\xcb\x93\x77\x45\x00\xa9\xcb" \
    +b"\x93\x07\xf6\x03\x99\x83\x2e\xc0\x2d\x63\x81\x76\x3e\xc4\xb7\x32" \
    +b"\x00\x40\xb7\x27\x02\x40\x13\x03\xa3\xaa\xfd\x16\x98\x4b\xb7\x03" \
    +b"\x02\x00\x33\x67\x77\x00\x98\xcb\x02\x47\xd8\xcb\x98\x4b\x13\x67" \
    +b"\x07\x04\x98\xcb\xd8\x47\x05\x8b\x69\xe7\x98\x4b\x75\x8f\x98\xcb" \
    +b"\x02\x47\x13\x07\x07\x04\x3a\xc0\x22\x47\x7d\x17\x3a\xc4\x79\xf7" \
    +b"\x93\x77\x85\x00\xf1\xcf\x93\x07\xf6\x03\x2e\xc0\x99\x83\x37\x27" \
    +b"\x02\x40\x3e\xc4\x1c\x4b\xc1\x66\x2d\x63\xd5\x8f\x1c\xcb\x37\x07" \
    +b"\x00\x20\x13\x07\x07\x20\xb7\x27\x02\x40\xb7\x03\x08\x00\xb7\x32" \
    +b"\x00\x40\x13\x03\xa3\xaa\x94\x4b\xb3\xe6\x76\x00\x94\xcb\xd4\x47" \
    +b"\x85\x8a\xf5\xfe\x82\x46\xba\x84\x37\x04\x04\x00\x36\xc2\xc1\x46" \
    +b"\x36\xc6\x92\x46\x84\x40\x11\x07\x84\xc2\x94\x4b\xc1\x8e\x94\xcb" \
    +b"\xd4\x47\x85\x8a\xb1\xea\x92\x46\xba\x84\x91\x06\x36\xc2\xb2\x46" \
    +b"\xfd\x16\x36\xc6\xf9\xfe\x82\x46\xd4\xcb\x94\x4b\x93\xe6\x06\x04" \
    +b"\x94\xcb\xd4\x47\x85\x8a\x85\xee\xd4\x47\xc1\x8a\x85\xce\xd8\x47" \
    +b"\xb7\x06\xf3\xff\xfd\x16\x13\x67\x07\x01\xd8\xc7\x98\x4b\x21\x45" \
    +b"\x75\x8f\x98\xcb\x52\x44\xc2\x44\x61\x01\x02\x90\x23\x20\xd3\x00" \
    +b"\xf5\xb5\x23\xa0\x62\x00\x3d\xb7\x23\xa0\x62\x00\x55\xb7\x23\xa0" \
    +b"\x62\x00\xc1\xb7\x82\x46\x93\x86\x06\x04\x36\xc0\xa2\x46\xfd\x16" \
    +b"\x36\xc4\xb5\xf2\x98\x4b\xb7\x06\xf3\xff\xfd\x16\x75\x8f\x98\xcb" \
    +b"\x41\x89\x05\xcd\x2e\xc0\x0d\x06\x02\xc4\x09\x82\xb7\x07\x00\x20" \
    +b"\x32\xc6\x93\x87\x07\x20\x98\x43\x13\x86\x47\x00\xa2\x47\x82\x46" \
    +b"\x8a\x07\xb6\x97\x9c\x43\x63\x1c\xf7\x00\xa2\x47\x85\x07\x3e\xc4" \
    +b"\xa2\x46\x32\x47\xb2\x87\xe3\xe0\xe6\xfe\x01\x45\x61\xb7\x41\x45" \
    +b"\x51\xb7"


BOOTLOADER203 = \
     b"\x93\x77\x15\x00\x41\x11\x99\xcf\xb7\x06\x67\x45\xb7\x27\x02\x40" \
    +b"\x93\x86\x36\x12\x37\x97\xef\xcd\xd4\xc3\x13\x07\xb7\x9a\xd8\xc3" \
    +b"\xd4\xd3\xd8\xd3\x93\x77\x25\x00\x95\xc7\xb7\x27\x02\x40\x98\x4b" \
    +b"\xad\x66\x37\x38\x00\x40\x13\x67\x47\x00\x98\xcb\x98\x4b\x93\x86" \
    +b"\xa6\xaa\x13\x67\x07\x04\x98\xcb\xd8\x47\x05\x8b\x61\xeb\x98\x4b" \
    +b"\x6d\x9b\x98\xcb\x93\x77\x45\x00\xa9\xcb\x93\x07\xf6\x0f\xa1\x83" \
    +b"\x2e\xc0\x2d\x68\x81\x76\x3e\xc4\xb7\x08\x02\x00\xb7\x27\x02\x40" \
    +b"\x37\x33\x00\x40\x13\x08\xa8\xaa\xfd\x16\x98\x4b\x33\x67\x17\x01" \
    +b"\x98\xcb\x02\x47\xd8\xcb\x98\x4b\x13\x67\x07\x04\x98\xcb\xd8\x47" \
    +b"\x05\x8b\x41\xeb\x98\x4b\x75\x8f\x98\xcb\x02\x47\x13\x07\x07\x10" \
    +b"\x3a\xc0\x22\x47\x7d\x17\x3a\xc4\x69\xfb\x93\x77\x85\x00\xd5\xcb" \
    +b"\x93\x07\xf6\x0f\x2e\xc0\xa1\x83\x3e\xc4\x37\x27\x02\x40\x1c\x4b" \
    +b"\xc1\x66\x41\x68\xd5\x8f\x1c\xcb\xb7\x16\x00\x20\xb7\x27\x02\x40" \
    +b"\x93\x08\x00\x04\x37\x03\x20\x00\x98\x4b\x33\x67\x07\x01\x98\xcb" \
    +b"\xd8\x47\x05\x8b\x75\xff\x02\x47\x3a\xc2\x46\xc6\x32\x47\x0d\xef" \
    +b"\x98\x4b\x33\x67\x67\x00\x98\xcb\xd8\x47\x05\x8b\x75\xff\xd8\x47" \
    +b"\x41\x8b\x39\xc3\xd8\x47\xc1\x76\xfd\x16\x13\x67\x07\x01\xd8\xc7" \
    +b"\x98\x4b\x21\x45\x75\x8f\x98\xcb\x41\x01\x02\x90\x23\x20\xd8\x00" \
    +b"\x25\xb7\x23\x20\x03\x01\xa5\xb7\x12\x47\x13\x8e\x46\x00\x94\x42" \
    +b"\x14\xc3\x12\x47\x11\x07\x3a\xc2\x32\x47\x7d\x17\x3a\xc6\xd8\x47" \
    +b"\x09\x8b\x75\xff\xf2\x86\x5d\xb7\x02\x47\x13\x07\x07\x10\x3a\xc0" \
    +b"\x22\x47\x7d\x17\x3a\xc4\x49\xf3\x98\x4b\xc1\x76\xfd\x16\x75\x8f" \
    +b"\x98\xcb\x41\x89\x15\xc9\x2e\xc0\x0d\x06\x02\xc4\x09\x82\x32\xc6" \
    +b"\xb7\x17\x00\x20\x98\x43\x13\x86\x47\x00\xa2\x47\x82\x46\x8a\x07" \
    +b"\xb6\x97\x9c\x43\x63\x1c\xf7\x00\xa2\x47\x85\x07\x3e\xc4\xa2\x46" \
    +b"\x32\x47\xb2\x87\xe3\xe0\xe6\xfe\x01\x45\xbd\xbf\x41\x45\xad\xbf"

# ===================================================================================

if __name__ == "__main__":
    _main()
