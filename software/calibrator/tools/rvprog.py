#!/usr/bin/env python3
# ===================================================================================
# Project:   rvprog - Programming Tool for WCH-LinkE and CH32Vxxx and CH32Xxxx.
# Version:   v1.5
# Year:      2023
# Author:    Stefan Wagner
# Github:    https://github.com/wagiminator
# License:   MIT License
# ===================================================================================
#
# Description:
# ------------
# Simple Python tool for flashing CH32V and CH32X microcontrollers using the WCH-LinkE
# or compatible programmers/debuggers. The code is based on CNLohr's minichlink.
# Currently supports: CH32V003, CH32V103, CH32V203, CH32V208, CH32V303, CH32V305, 
#                     CH32V307, CH32X033, CH32X035.
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
# - python3 rvprog.py [-h] [-a] [-v] [-b] [-u] [-l] [-e] [-G] [-R] [-f FLASH]
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
#   python3 rvprog.py -f firmware.bin


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
    parser.add_argument('-a', '--armmode',  action='store_true', help='switch WCH-LinkE to ARM mode')
    parser.add_argument('-v', '--rvmode',   action='store_true', help='switch WCH-LinkE to RISC-V mode')
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
            print('Searching for WCH-LinkE in ARM mode ...')
            armlink = usb.core.find(idVendor = CH_VENDOR_ID, idProduct = CH_ARM_ID)
            if armlink is None:
                raise Exception('No WCH-LinkE in ARM mode found!')
            print('SUCCESS: Found WCH-LinkE in ARM mode.')
            print('Switching WCH-LinkE to RISC-V mode ...')
            armlink.write(0x02, b'\x81\xff\x01\x52')
            time.sleep(2)
            print('DONE.')
    except Exception as ex:
        sys.stderr.write('ERROR: %s!\n' % str(ex))

    # Establish connection to WCH-Link
    try:
        print('Searching for WCH-LinkE in RISC-V mode ...')
        isp = Programmer()
        print('SUCCESS: Found %s v%s in RISC-V mode.' % (isp.linkname, isp.linkversion))
    except Exception as ex:
        sys.stderr.write('ERROR: %s!\n' % str(ex))
        sys.exit(1)

    # Performing actions
    try:
        # Unbrick chip
        if args.unbrick:
            print('Unbricking chip ...')
            isp.unbrick()
            print('SUCCESS: Chip is unbricked.')

        # Establish connection to target MCU
        if any( (args.unlock, args.lock, args.erase, args.flash, args.pingpio, args.pinreset) ):
            print('Connecting to MCU ...')
            isp.connect()
            print('SUCCESS: Connected to %s with %d bytes of flash.' % (isp.chipname, isp.flashsize))

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
            print('Flashing %s to %s ...' % (args.flash, isp.chipname))
            with open(args.flash, 'rb') as f: data = f.read()
            isp.flash_data(data)
            print('SUCCESS: %d bytes written and verified.' % len(data))

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
        sys.stderr.write('ERROR: %s!\n' % str(ex))
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
            raise Exception('WCH-LinkE not found. Check if device is in RISC-V mode')

        # Clear receive buffers
        self.clearreply()

        # Get programmer info
        reply = self.sendcommand(b'\x81\x0d\x01\x01')
        if not reply[5] == 0x12:
            raise Exception('Programmer is not a WCH-LinkE')
        self.linkname    = 'WCH-LinkE'
        self.linkdevice  = reply[5]
        self.linkversion = '%d.%d' % (reply[3], reply[4])

    # Connect programmer to MCU
    def connect(self):
        # Connect to target MCU and get type
        success = 0
        for i in range(3):
            if i > 0:
                print('Failed to connect, trying to unbrick MCU ...')
                self.sendcommand(b'\x81\x0d\x01\x0f\x09')
            reply = self.sendcommand(b'\x81\x0d\x01\x02')
            if len(reply) < 8 or set(reply[:4]) == set((0x81, 0x55, 0x01, 0x01)):
                time.sleep(0.2)
                continue

            # Get chip identification data
            self.chipmark   = reply[3]
            self.chipseries = reply[4]
            self.chiptype   = reply[5]
            self.chipid     = (reply[4] << 8) + reply[5]
            self.device     = None

            # Find device in dictionary
            for d in DEVICES:
                if d['id'] == self.chipid:
                    self.device = d
            if self.device is None:
                if not self.chipid == 0:
                    print('Unsupported chip (ID: 0x%04x)?' % self.chipid)
                continue

            # Success
            self.chipname = self.device['name']
            success = 1
            break
        if success == 0:
            raise Exception('Failed to connect to target MCU')

        # Read some chip data
        reply = self.sendcommand((0x81, 0x11, 0x01, self.chipmark))
        self.flashsize = int.from_bytes(reply[2:4], byteorder='big') * 1024

    # Send command to programmer
    def sendcommand(self, stream):
        self.dev.write(CH_EP_OUT, stream)
        return self.dev.read(CH_EP_IN, CH_PACKET_SIZE, CH_TIMEOUT)

    # Clear USB receive buffers
    def clearreply(self):
        try:    self.dev.read(CH_EP_IN, CH_PACKET_SIZE, 1)
        except: None
        try:    self.dev.read(CH_EP_IN_RAW, CH_PACKET_SIZE, 1)
        except: None

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
        self.sendcommand(b'\x81\x0d\x01\x0f\x09')

    # Lock MCU (set read protection)
    def lock(self):
        if self.chipseries == 0x00:
            self.sendcommand(b'\x81\x06\x08\x03\xf7\xff\xff\xff\xff\xff\xff')
        elif self.chipseries == 0x25 or self.chipseries == 0x03:
            self.sendcommand(b'\x81\x06\x08\x03\xff\xff\xff\xff\xff\xff\xff')
        elif self.chipseries == 0x20 or self.chipseries == 0x30:
            self.sendcommand(b'\x81\x06\x08\x03\x3f\xff\xff\xff\xff\xff\xff')

    # Unlock MCU (remove read protection -> erase chip!!!)
    def unlock(self):
        reply = self.sendcommand(b'\x81\x06\x01\x01')
        if reply[3] == 0x01:
            if self.chipseries == 0x00:
                self.sendcommand(b'\x81\x06\x08\x02\xf7\xff\xff\xff\xff\xff\xff')
            elif self.chipseries == 0x25 or self.chipseries == 0x03:
                self.sendcommand(b'\x81\x06\x08\x02\xff\xff\xff\xff\xff\xff\xff')
            elif self.chipseries == 0x20 or self.chipseries == 0x30:
                self.sendcommand(b'\x81\x06\x08\x02\x3f\xff\xff\xff\xff\xff\xff')

    # Perform a whole chip erase
    def erase(self):
        self.sendcommand(b'\x81\x02\x01\x01')
        self.sendcommand(b'\x81\x0d\x01\x02')

    # Make reset pin a normal GPIO pin (0=RESET, 1=GPIO)
    def setnrstasgpio(self, state):
        if self.chipseries == 0x00:
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
        result = list()
        while len(data):
            result.append(data[:pagesize])
            data = data[pagesize:]
        return result

    # Write data blob to flash
    def writebinaryblob(self, addr, blocksize, bootloader, data):
        if addr & (blocksize - 1):
            raise Exception('Blob is not %d byte aligned' % blocksize)
        self.unlock()
        stream = b'\x81\x01\x08' \
               + addr.to_bytes(4, byteorder='big') \
               + self.padlen(data, blocksize).to_bytes(4, byteorder='big')
        self.sendcommand(stream)
        self.sendcommand(b'\x81\x02\x01\x05')
        pages = self.page_data(bootloader, 128)
        for page in pages:
            self.dev.write(CH_EP_OUT_RAW, page)
        self.sendcommand(b'\x81\x02\x01\x07')
        self.sendcommand(b'\x81\x02\x01\x04')
        pages = self.page_data(data, blocksize)
        for page in pages:
            self.dev.write(CH_EP_OUT_RAW, page)
        reply = self.dev.read(CH_EP_IN_RAW, CH_PACKET_SIZE, CH_TIMEOUT)
        if set(reply) != set((0x41, 0x01, 0x01, 0x04)):
            raise Exception('Failed writing/verifying data blob')

    # Write data to code flash
    def flash_data(self, data):
        if len(data) > self.flashsize:
            raise Exception('Not enough memory')
        self.writebinaryblob(CH_CODE_BASE, self.device['block-size'], self.device['loader'], data)

# ===================================================================================
# Device Constants
# ===================================================================================

# USB device settings
CH_VENDOR_ID    = 0x1A86    # VID
CH_PRODUCT_ID   = 0x8010    # PID in RISC-V mode
CH_ARM_ID       = 0x8012    # PID in ARM mode
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
CH_BOOT_BASE    = 0x1FFFF000

# ===================================================================================
# Flash Bootloader
# ===================================================================================

LOADER_V003 = \
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

LOADER_V103 = \
     b"\x93\x77\x15\x00\x41\x11\x99\xcf\xb7\x06\x67\x45\xb7\x27\x02\x40" \
    +b"\x93\x86\x36\x12\x37\x97\xef\xcd\xd4\xc3\x13\x07\xb7\x9a\xd8\xc3" \
    +b"\xd4\xd3\xd8\xd3\x93\x77\x25\x00\x9d\xc7\xb7\x27\x02\x40\x98\x4b" \
    +b"\xad\x66\x37\x38\x00\x40\x13\x67\x47\x00\x98\xcb\x98\x4b\x93\x86" \
    +b"\xa6\xaa\x13\x67\x07\x04\x98\xcb\xd8\x47\x05\x8b\x63\x1f\x07\x10" \
    +b"\x98\x4b\x6d\x9b\x98\xcb\x93\x77\x45\x00\xa9\xcb\x93\x07\xf6\x07" \
    +b"\x9d\x83\x2e\xc0\x2d\x68\x81\x76\x3e\xc4\xb7\x08\x02\x00\xb7\x27" \
    +b"\x02\x40\x37\x33\x00\x40\x13\x08\xa8\xaa\xfd\x16\x98\x4b\x33\x67" \
    +b"\x17\x01\x98\xcb\x02\x47\xd8\xcb\x98\x4b\x13\x67\x07\x04\x98\xcb" \
    +b"\xd8\x47\x05\x8b\x71\xef\x98\x4b\x75\x8f\x98\xcb\x02\x47\x13\x07" \
    +b"\x07\x08\x3a\xc0\x22\x47\x7d\x17\x3a\xc4\x69\xfb\x93\x77\x85\x00" \
    +b"\xed\xc3\x93\x07\xf6\x07\x2e\xc0\x9d\x83\x37\x27\x02\x40\x3e\xc4" \
    +b"\x1c\x4b\xc1\x66\x37\x08\x08\x00\xd5\x8f\x1c\xcb\xa1\x48\x37\x17" \
    +b"\x00\x20\xb7\x27\x02\x40\x37\x03\x04\x00\x94\x4b\xb3\xe6\x06\x01" \
    +b"\x94\xcb\xd4\x47\x85\x8a\xf5\xfe\x82\x46\x3a\x8e\x36\xc2\x46\xc6" \
    +b"\x92\x46\x83\x2e\x07\x00\x41\x07\x23\xa0\xd6\x01\x92\x46\x83\x2e" \
    +b"\x47\xff\x23\xa2\xd6\x01\x92\x46\x83\x2e\x87\xff\x23\xa4\xd6\x01" \
    +b"\x92\x46\x03\x2e\xce\x00\x23\xa6\xc6\x01\x94\x4b\xb3\xe6\x66\x00" \
    +b"\x94\xcb\xd4\x47\x85\x8a\xf5\xfe\x92\x46\x3a\x8e\xc1\x06\x36\xc2" \
    +b"\xb2\x46\xfd\x16\x36\xc6\xcd\xfe\x82\x46\xd4\xcb\x94\x4b\x93\xe6" \
    +b"\x06\x04\x94\xcb\xd4\x47\x85\x8a\xf5\xfe\xd4\x47\xd1\x8a\x85\xc6" \
    +b"\xd8\x47\xb7\x06\xf3\xff\xfd\x16\x13\x67\x47\x01\xd8\xc7\x98\x4b" \
    +b"\x21\x45\x75\x8f\x98\xcb\x41\x01\x02\x90\x23\x20\xd8\x00\xe9\xbd" \
    +b"\x23\x20\x03\x01\x31\xbf\x82\x46\x93\x86\x06\x08\x36\xc0\xa2\x46" \
    +b"\xfd\x16\x36\xc4\xb9\xfa\x98\x4b\xb7\x06\xf3\xff\xfd\x16\x75\x8f" \
    +b"\x98\xcb\x41\x89\x15\xc9\x2e\xc0\x0d\x06\x02\xc4\x09\x82\x32\xc6" \
    +b"\xb7\x17\x00\x20\x98\x43\x13\x86\x47\x00\xa2\x47\x82\x46\x8a\x07" \
    +b"\xb6\x97\x9c\x43\x63\x1c\xf7\x00\xa2\x47\x85\x07\x3e\xc4\xa2\x46" \
    +b"\x32\x47\xb2\x87\xe3\xe0\xe6\xfe\x01\x45\x71\xbf\x41\x45\x61\xbf"

LOADER_V203 = \
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

LOADER_X035 = \
     b"\x01\x11\x02\xce\x93\x77\x15\x00\x99\xcf\xb7\x06\x67\x45\xb7\x27" \
    +b"\x02\x40\x93\x86\x36\x12\x37\x97\xef\xcd\xd4\xc3\x13\x07\xb7\x9a" \
    +b"\xd8\xc3\xd4\xd3\xd8\xd3\x93\x77\x25\x00\x9d\xc7\xb7\x27\x02\x40" \
    +b"\x98\x4b\xad\x66\x37\x38\x00\x40\x13\x67\x47\x00\x98\xcb\x98\x4b" \
    +b"\x93\x86\xa6\xaa\x13\x67\x07\x04\x98\xcb\xd8\x47\x05\x8b\x63\x16" \
    +b"\x07\x10\x98\x4b\x6d\x9b\x98\xcb\x93\x77\x45\x00\xa9\xcb\x93\x07" \
    +b"\xf6\x0f\xa1\x83\x2e\xc6\x2d\x68\x81\x76\x3e\xca\xb7\x08\x02\x00" \
    +b"\xb7\x27\x02\x40\x37\x33\x00\x40\x13\x08\xa8\xaa\xfd\x16\x98\x4b" \
    +b"\x33\x67\x17\x01\x98\xcb\x32\x47\xd8\xcb\x98\x4b\x13\x67\x07\x04" \
    +b"\x98\xcb\xd8\x47\x05\x8b\x69\xe7\x98\x4b\x75\x8f\x98\xcb\x32\x47" \
    +b"\x13\x07\x07\x10\x3a\xc6\x52\x47\x7d\x17\x3a\xca\x69\xfb\x93\x77" \
    +b"\x85\x00\xf1\xcf\x93\x07\xf6\x0f\x2e\xc6\xa1\x83\x3e\xca\x37\x27" \
    +b"\x02\x40\x1c\x4b\xc1\x66\x2d\x68\xd5\x8f\x1c\xcb\xb7\x16\x00\x20" \
    +b"\xb7\x27\x02\x40\x37\x03\x08\x00\x13\x0e\x00\x04\xb7\x0e\x04\x00" \
    +b"\xb7\x38\x00\x40\x13\x08\xa8\xaa\x98\x4b\x33\x67\x67\x00\x98\xcb" \
    +b"\xd8\x47\x05\x8b\x75\xff\x32\x47\x36\x8f\x3a\xc8\x72\xcc\x42\x47" \
    +b"\x03\x2f\x0f\x00\x91\x06\x23\x20\xe7\x01\x98\x4b\x33\x67\xd7\x01" \
    +b"\x98\xcb\xd8\x47\x05\x8b\x21\xeb\x42\x47\x36\x8f\x11\x07\x3a\xc8" \
    +b"\x62\x47\x7d\x17\x3a\xcc\x61\xff\x32\x47\xd8\xcb\x98\x4b\x13\x67" \
    +b"\x07\x04\x98\xcb\xd8\x47\x05\x8b\x15\xeb\xd8\x47\x41\x8b\x15\xcb" \
    +b"\xd8\x47\xb7\x06\xf3\xff\xfd\x16\x13\x67\x07\x01\xd8\xc7\x98\x4b" \
    +b"\x21\x45\x75\x8f\x98\xcb\x05\x61\x02\x90\x23\x20\xd8\x00\xf5\xb5" \
    +b"\x23\x20\x03\x01\x3d\xb7\x23\xa0\x08\x01\x65\xb7\x23\xa0\x08\x01" \
    +b"\xd1\xb7\x32\x47\x13\x07\x07\x10\x3a\xc6\x52\x47\x7d\x17\x3a\xca" \
    +b"\x25\xf7\x98\x4b\xb7\x06\xf3\xff\xfd\x16\x75\x8f\x98\xcb\x41\x89" \
    +b"\x19\xe1\x01\x45\xc9\xb7\x2e\xc6\x0d\x06\x02\xca\x09\x82\x32\xcc" \
    +b"\xb7\x17\x00\x20\x98\x43\x13\x86\x47\x00\xd2\x47\xb2\x46\x8a\x07" \
    +b"\xb6\x97\x9c\x43\x63\x18\xf7\x02\xd2\x47\x32\x47\x8a\x07\xba\x97" \
    +b"\x98\x43\xf2\x47\xba\x97\x3e\xce\xd2\x47\x85\x07\x3e\xca\xd2\x46" \
    +b"\x62\x47\xb2\x87\xe3\xe8\xe6\xfc\xb7\x27\x00\x20\x98\x4b\xf2\x47" \
    +b"\xe3\x09\xf7\xfa\x41\x45\x85\xbf"

# ===================================================================================
# Device definitions
# ===================================================================================

DEVICES = [
    {'name': 'CH32V003J4M6', 'id': 0x0033, 'block-size':  64, 'loader': LOADER_V003},
    {'name': 'CH32V003A4M6', 'id': 0x0032, 'block-size':  64, 'loader': LOADER_V003},
    {'name': 'CH32V003F4U6', 'id': 0x0031, 'block-size':  64, 'loader': LOADER_V003},
    {'name': 'CH32V003F4P6', 'id': 0x0030, 'block-size':  64, 'loader': LOADER_V003},

    {'name': 'CH32V103',     'id': 0x2500, 'block-size': 128, 'loader': LOADER_V103},

    {'name': 'CH32V203C8U6', 'id': 0x2030, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V203C8T6', 'id': 0x2031, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V203K8T6', 'id': 0x2032, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V203C6T6', 'id': 0x2033, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V203RBT6', 'id': 0x2034, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V203K6T6', 'id': 0x2035, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V203G6U6', 'id': 0x2036, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V203F6P6', 'id': 0x2037, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V203F8P6', 'id': 0x203a, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V203G8R6', 'id': 0x203b, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V208WBU6', 'id': 0x2080, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V208RBT6', 'id': 0x2081, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V208CBU6', 'id': 0x2082, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V208GBU6', 'id': 0x2083, 'block-size': 256, 'loader': LOADER_V203},

    {'name': 'CH32V303VCT6', 'id': 0x3030, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V303RCT6', 'id': 0x3031, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V303RBT6', 'id': 0x3033, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V303CBT6', 'id': 0x3034, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V305RBT6', 'id': 0x3050, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V305FBP6', 'id': 0x3052, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V307VCT6', 'id': 0x3070, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V307RCT6', 'id': 0x3071, 'block-size': 256, 'loader': LOADER_V203},
    {'name': 'CH32V307WCU6', 'id': 0x3073, 'block-size': 256, 'loader': LOADER_V203},

    {'name': 'CH32X033F8P6', 'id': 0x035a, 'block-size': 256, 'loader': LOADER_X035},
    {'name': 'CH32X035R8T6', 'id': 0x0350, 'block-size': 256, 'loader': LOADER_X035},
    {'name': 'CH32X035C8T6', 'id': 0x0351, 'block-size': 256, 'loader': LOADER_X035},
    {'name': 'CH32X035F8U6', 'id': 0x035e, 'block-size': 256, 'loader': LOADER_X035},
    {'name': 'CH32X035G8U6', 'id': 0x0356, 'block-size': 256, 'loader': LOADER_X035},
    {'name': 'CH32X035G8R6', 'id': 0x035b, 'block-size': 256, 'loader': LOADER_X035},
    {'name': 'CH32X035F7P6', 'id': 0x0357, 'block-size': 256, 'loader': LOADER_X035}
]

# ===================================================================================

if __name__ == "__main__":
    _main()
