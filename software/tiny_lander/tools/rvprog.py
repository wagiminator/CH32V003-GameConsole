#!/usr/bin/env python3
# ===================================================================================
# Project:   rvprog - Programming Tool for WCH-LinkE and CH32V003
# Version:   v1.1
# Year:      2023
# Author:    Stefan Wagner
# Github:    https://github.com/wagiminator
# License:   MIT License
# ===================================================================================
#
# Description:
# ------------
# Simple Python tool for flashing CH32V003 microcontrollers using the WCH-LinkE or
# compatible programmers/debuggers. The code is heavily based on CNLohr's minichlink.
#
# References:
# -----------
# - CNLohr minichlink: https://github.com/cnlohr/ch32v003fun/tree/master/minichlink
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
# Connect the WCH-LinkE to your PC and to your CH32V003 board. The WCH-LinkE must be 
# in LinkRV mode (blue LED off)! If not, run: python rvprog.py -v
# Run:
# - python rvprog.py [-h] [-e] [-f BIN] [-g] [-G] [-u]
#   -h, --help                show help message and exit
#   -a, --armmode             switch WCH-LinkE to ARM mode
#   -v, --rvmode              switch WCH-LinkE to RISC-V mode
#   -u, --unlock              unlock (unbrick) chip
#   -e, --erase               perform a whole chip erase
#   -p, --pingpio             make nRST pin a GPIO pin
#   -P, --pinreset            make nRST pin a reset pin
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
    parser = argparse.ArgumentParser(description='Minimal command line interface for CH32V003 programming')
    parser.add_argument('-a', '--armmode',  action='store_true', help='switch WCH-Link to ARM mode')
    parser.add_argument('-v', '--rvmode',   action='store_true', help='switch WCH-Link to RISC-V mode')
    parser.add_argument('-u', '--unlock',   action='store_true', help='unlock (unbrick) chip')
    parser.add_argument('-e', '--erase',    action='store_true', help='perform a whole chip erase')
    parser.add_argument('-p', '--pingpio',  action='store_true', help='make nRST pin a GPIO pin')
    parser.add_argument('-P', '--pinreset', action='store_true', help='make nRST pin a reset pin')
    parser.add_argument('-f', '--flash', help='write BIN file to flash')
    args = parser.parse_args(sys.argv[1:])

    # Check arguments
    if not any( (args.armmode, args.erase, args.flash, args.pingpio, args.pinreset, args.unlock, args.rvmode) ):
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

    # Establish connection to WCH-LinkE
    try:
        print('Searching for WCH-LinkE in RISC-V mode ...')
        isp = Programmer()
        print('SUCCESS: Found WCH-LinkE v' + isp.linkversion + ' in RISC-V mode.')
    except Exception as ex:
        sys.stderr.write('ERROR: ' + str(ex) + '!\n')
        sys.exit(1)

    # Establish connection to CH32V003
    try:
        if any( (args.erase, args.flash, args.pingpio, args.pinreset, args.unlock) ):
            print('Connecting to MCU ...')
            isp.connect()
            print('SUCCESS: Connected to CH32V003.')
    except Exception as ex:
        sys.stderr.write('ERROR: ' + str(ex) + '!\n')
        isp.exit()
        sys.exit(1)

    # Performing actions
    try:
        # Unlock chip
        if args.unlock:
            print('Unlocking chip ...')
            isp.unbrick()
            print('SUCCESS: Chip is unlocked.')

        # Perform chip erase
        if args.erase:
            isp.sethaltmode(0)
            print('Performing whole chip erase ...')
            isp.erasechip()
            print('SUCCESS: Chip is erased.')

        # Flash binary file
        if args.flash is not None:
            isp.sethaltmode(0)
            print('Flashing', args.flash, 'to CH32V003 ...')
            with open(args.flash, 'rb') as f: data = f.read()
            isp.flash_data(data)
            print('SUCCESS:', len(data), 'bytes written.')
            print('Verifying ...')
            isp.verify_data(data)
            print('SUCCESS:', len(data), 'bytes verified.')

        # Make nRST pin a normal GPIO pin
        if args.pingpio:
            isp.sethaltmode(0)
            print('Configuring nRST pin as GPIO ...')
            isp.setnrstasgpio(1)
            print('SUCCESS: nRST pin is now a GPIO pin.')

        # Make nRST pin a reset pin
        if args.pinreset:
            isp.sethaltmode(0)
            print('Configuring nRST pin as reset ...')
            isp.setnrstasgpio(0)
            print('SUCCESS: nRST pin is now a reset pin.')

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

        # Clear receive buffer
        self.clearreply()

        # Get programmer info
        reply = self.sendcommand(b'\x81\x0d\x01\x01')
        if reply[5] != 18:
          raise Exception('Programmer is not a WCH-LinkE')
        self.linkversion = str(reply[3]) + '.' + str(reply[4])

    # Connect programmer to MCU
    def connect(self):
        # Put MCU to hold to allow debugger to run. Get target MCU type.
        reply = self.sendcommand(b'\x81\x0d\x01\x02')
        if len(reply) < 8 or reply[:4] == (0x81, 0x55, 0x01, 0x01):
          raise Exception('No MCU is connected to device')
        self.chiptype = (reply[4]<<4) + (reply[5]>>4)
        if self.chiptype != 0x003:
          raise Exception('Target MCU is not a CH32V003')

        # For some reason, it's better to do this
        self.writereg(DMCONTROL,      0x80000001)
        self.writereg(DMCONTROL,      0x80000001)
        self.writereg(DMCONTROL,      0x80000001)
        self.writereg(DMABSTRACTCS,   0x00000700)
        self.writereg(DMABSTRACTAUTO, 0x00000000)
        self.writereg(DMCOMMAND,      0x00221000)
        self.waitopdone()

        # Read some chip data (maybe someday this will become useful)
        reply = self.sendcommand(b'\x81\x11\x01\x09')

        # Setup internal flags
        self.statetag       = 'STRT'
        self.stateval       = -1
        self.haltmode       = -1
        self.flashunlocked  = 0
        self.autoincrement  = 0
        self.lastwriteflags = 0

    # Send command to programmer
    def sendcommand(self, stream):
        self.dev.write(CH_EP_OUT, stream)
        return self.dev.read(CH_EP_IN, CH_PACKET_SIZE, CH_TIMEOUT)

    # Clear USB receive buffer
    def clearreply(self):
        try:
            self.dev.read(CH_EP_IN, CH_PACKET_SIZE, 1)
        except:
            None

    # Write to MCU register
    def writereg(self, addr, data):
        stream = bytes((0x81, 0x08, 0x06, addr&0x7F)) + data.to_bytes(4, byteorder='big') + b'\x02'
        reply = self.sendcommand(stream)
        if (len(reply) != 9) or (reply[3] != addr&0x7F):
            raise Exception('Failed to write register')

    # Read from MCU register
    def readreg(self, addr):
        stream = (0x81, 0x08, 0x06, addr&0x7F, 0, 0, 0, 0, 1)
        reply = self.sendcommand(stream)
        return int.from_bytes(reply[4:8], byteorder='big')

    # Unlock MCU
    def unbrick(self):
        self.sendcommand(b'\x81\x0d\x01\x0f\x09')

    # Configure read protection (0=unprotect, 1=protect)
    def setreadprotect(self, state):
        if state:
            self.sendcommand(b'\x81\x06\x08\x03\xf7\xff\xff\xff\xff\xff\xff')
            self.sendcommand(b'\x81\x0b\x01\x01')
        else:
            self.sendcommand(b'\x81\x06\x08\x02\xf7\xff\xff\xff\xff\xff\xff')
            self.sendcommand(b'\x81\x0b\x01\x01')

    # Make reset pin a normal GPIO pin (0=RESET, 1=GPIO)
    def setnrstasgpio(self, state):
        if state:
            self.sendcommand(b'\x81\x06\x08\x02\xff\xff\xff\xff\xff\xff\xff')
            self.sendcommand(b'\x81\x0b\x01\x01')
        else:
            self.sendcommand(b'\x81\x06\x08\x02\xf7\xff\xff\xff\xff\xff\xff')
            self.sendcommand(b'\x81\x0b\x01\x01')

    # Enable/disable programmer's 3V3 output
    def control3v3(self, state):
        if state:
            self.sendcommand(b'\x81\x0d\x01\x09')
        else:
            self.sendcommand(b'\x81\x0d\x01\x0a')

    # Enable/disable programmer's 5V output
    def control5v(self, state):
        if state:
            self.sendcommand(b'\x81\x0d\x01\x0b')
        else:
            self.sendcommand(b'\x81\x0d\x01\x0c')

    # Disconnect from MCU
    def exit(self):
        self.sendcommand(b'\x81\x0d\x01\x03')
        self.sendcommand(b'\x81\x0d\x01\xff')

    #--------------------------------------------------------------

    # Set MCU halt mode
    def sethaltmode(self, mode):
        if (mode == 0) or (mode == 5):
            self.writereg(DMSHDWCFGR, 0x5aa50000 | (1<<10))
            self.writereg(DMCFGR,     0x5aa50000 | (1<<10))
            self.writereg(DMCFGR,     0x5aa50000 | (1<<10))
            self.writereg(DMCONTROL,  0x80000001)
            if mode == 0:
                self.writereg(DMCONTROL, 0x80000003)
            self.writereg(DMCONTROL,  0x80000001)
        elif mode == 1:
            self.writereg(DMCONTROL,  0x80000001)
            self.writereg(DMCONTROL,  0x80000001)
            self.writereg(DMCONTROL,  0x80000003)
            self.writereg(DMCONTROL,  0x40000001)
        elif mode == 2:
            self.writereg(DMSHDWCFGR, 0x5aa50000 | (1<<10))
            self.writereg(DMCFGR,     0x5aa50000 | (1<<10))
            self.writereg(DMCFGR,     0x5aa50000 | (1<<10))
            self.writereg(DMCONTROL,  0x40000001)
        elif mode == 3:
            self.writereg(DMCONTROL,  0x80000001)
            self.writereg(DMCONTROL,  0x80000001)
            self.writeword(0x40022004, 0x45670123)
            self.writeword(0x40022004, 0xCDEF89AB)
            self.writeword(0x40022028, 0x45670123)
            self.writeword(0x40022028, 0xCDEF89AB)
            self.writeword(0x4002200C, 1<<14)
            self.writeword(0x40022010, 1<<7)
            self.writereg(DMCONTROL,  0x80000003)
            self.writereg(DMCONTROL,  0x40000001)
        else:
            raise Exception('Unknown halt mode')
        self.haltmode = mode

    # Update PROGBUF registers
    def updateprogbufregs(self):
        reg = self.readreg(DMHARTINFO)
        data0offset = 0xe0000000 | ( reg & 0x7ff )
        self.writereg(DMDATA0,   data0offset)
        self.writereg(DMCOMMAND, 0x0023100a)
        self.writereg(DMDATA0,   data0offset + 4)
        self.writereg(DMCOMMAND, 0x0023100b)
        self.writereg(DMDATA0,   0x40022010)
        self.writereg(DMCOMMAND, 0x0023100c)
        self.writereg(DMDATA0,   0x00050000)
        self.writereg(DMCOMMAND, 0x0023100d)

    # Wait for last operation finished
    def waitopdone(self):
        while True:
            reg = self.readreg(DMABSTRACTCS)
            if (reg & (1 << 12)) == 0:
                break
        if ((reg >> 8) & 7):
            self.writereg(DMABSTRACTCS, 0x00000700)
            raise Exception('Failed to conduct operation')

    # Wait for flash page written
    def waitforflash(self):
        timeout = 100
        while True:
            reg = self.readword(0x4002200C)
            if (reg & 1) == 0:
                break
            timeout -= 1
            if timeout == 0:
                raise Exception('Timout waiting for FLASH')
        if (reg & 0x10) > 0:
            raise Exception('FLASH is protected')

    # Unlock flash
    def unlockflash(self):
        reg = self.readword(0x40022010)
        if (reg & 0x8080):
            self.writeword(0x40022004, 0x45670123)
            self.writeword(0x40022004, 0xCDEF89AB)
            self.writeword(0x40022008, 0x45670123)
            self.writeword(0x40022008, 0xCDEF89AB)
            self.writeword(0x40022024, 0x45670123)
            self.writeword(0x40022024, 0xCDEF89AB)
            reg = self.readword(0x40022010)
            if (reg & 0x8080):
                raise Exception('Failed to unlock FLASH')
        self.flashunlocked = 1

    # Unlock boot sector
    def unlockboot(self):
        self.writeword(0x40022028, 0x45670123)
        self.writeword(0x40022028, 0xCDEF89AB)
        obtkeyr = self.readword(0x40022008)
        if (obtkeyr & (1<<15)):
            raise Exception('Failed to unlock BOOT sector')
        obtkeyr |= (1<<14)
        self.writeword(0x40022008, obtkeyr)

    # Whole-chip erase
    def erasechip(self):
        if self.flashunlocked < 1:
            self.unlockflash()
        self.statetag = 'XXXX'
        self.writeword(0x40022010, 1<<2)
        self.writeword(0x40022010, (1<<2) | (1<<6))
        self.waitforflash()
        self.writeword(0x40022010, 0)

    # Erase flash page
    def erasepage(self, base):
        if self.flashunlocked < 1:
            self.unlockflash()
        self.writeword(0x40022010, 1<<17)
        self.writeword(0x40022014, base)
        self.writeword(0x40022010, (1<<17) | (1<<6))
        self.waitforflash()
        self.writeword(0x40022010, 0)

    # Write 8-bit byte to flash
    def writebyte(self, addr, data):
        self.statetag = 'XXXX'
        self.writereg(DMABSTRACTAUTO, 0x00848023)
        self.writereg(DMPROGBUF0,     0x00049403)
        self.writereg(DMPROGBUF1,     0x00100073)
        self.writereg(DMDATA0,        addr)
        self.writereg(DMCOMMAND,      0x00231009)
        self.writereg(DMDATA0,        data)
        self.writereg(DMCOMMAND,      0x00271008)
        self.waitopdone()
        self.stateval = -1

    # Read 8-bit byte to flash
    def readbyte(self, addr):
        self.statetag = 'XXXX'
        self.writereg(DMABSTRACTAUTO, 0x00000000)
        self.writereg(DMPROGBUF0,     0x00048403)
        self.writereg(DMPROGBUF1,     0x00100073)
        self.writereg(DMDATA0,        addr)
        self.writereg(DMCOMMAND,      0x00231009)
        self.writereg(DMCOMMAND,      0x00241000)
        self.writereg(DMCOMMAND,      0x00221008)
        self.waitopdone()
        self.stateval = -1
        return self.readreg(DMDATA0)

    # Write 16-bit halfword
    def writehalfword(self, addr, data):
        self.statetag = 'XXXX'
        self.writereg(DMABSTRACTAUTO, 0x00000000)
        self.writereg(DMPROGBUF0,     0x00849023)
        self.writereg(DMPROGBUF1,     0x00100073)
        self.writereg(DMDATA0,        addr)
        self.writereg(DMCOMMAND,      0x00231009)
        self.writereg(DMDATA0,        data)
        self.writereg(DMCOMMAND,      0x00271008)
        self.waitopdone()
        self.stateval = -1

    # Read 16-bit halfword
    def readhalfword(self, addr):
        self.statetag = 'XXXX'
        self.writereg(DMABSTRACTAUTO, 0x00000000)
        self.writereg(DMPROGBUF0,     0x00049403)
        self.writereg(DMPROGBUF1,     0x00100073)
        self.writereg(DMDATA0, addr)
        self.writereg(DMCOMMAND,      0x00231009)
        self.writereg(DMCOMMAND,      0x00241000)
        self.writereg(DMCOMMAND,      0x00221008)
        self.waitopdone()
        self.stateval = -1
        return self.readreg(DMDATA0)

    # Write 32-bit word
    def writeword(self, addr, data):
        isflash = 0
        if ((addr & 0xff000000) == 0x08000000) or ((addr & 0x1FFFF800) == 0x1FFFF000):
            isflash = 1
        if (self.statetag != 'WRSQ') or (isflash != self.lastwriteflags):
            diddisablereq = 0
            if self.statetag != 'WRSQ':
                self.writereg(DMABSTRACTAUTO, 0)
                diddisablereq = 1
                self.writereg(DMPROGBUF0, 0xc0804184)
                self.writereg(DMPROGBUF1, 0xc1840491)
                if self.statetag != 'RDSQ':
                    self.updateprogbufregs()
            if(isflash != self.lastwriteflags) or (self.statetag != 'WRSQ'):
                if(isflash):
                    self.writereg(DMPROGBUF2, 0x9002c214)
                else:
                    self.writereg(DMPROGBUF2, 0x00019002)
            self.writereg(DMDATA1, addr)
            self.writereg(DMDATA0, data)
            if diddisablereq:
                self.writereg(DMCOMMAND, 0x00271008)
                self.writereg(DMABSTRACTAUTO, 1)
            self.statetag = 'WRSQ'
            self.stateval = addr
            if isflash:
                self.waitopdone()
        else:
            if addr != self.stateval:
                self.writereg(DMABSTRACTAUTO, 0)
                self.writereg(DMDATA1, addr)
                self.writereg(DMABSTRACTAUTO, 1)
                self.stateval = addr
            self.writereg(DMDATA0, data)
            self.waitopdone()
        self.stateval += 4

    # Read 32-bit word
    def readword(self, addr):
        autoincrement = 1
        if (addr == 0x40022010) or (addr == 0x4002200C):
            autoincrement = 0
        if (self.statetag != 'RDSQ') or (addr != self.stateval) or (autoincrement != self.autoincrement):
            if self.statetag != 'RDSQ':
                self.writereg(DMABSTRACTAUTO, 0)
                self.writereg(DMPROGBUF0, 0x40044180)
                if autoincrement:
                    self.writereg(DMPROGBUF1, 0xc1040411)
                else:
                    self.writereg(DMPROGBUF1, 0xc1040001)
                self.writereg(DMPROGBUF2, 0x9002c180)
                if self.statetag != 'WRSQ':
                    self.updateprogbufregs()
                self.writereg(DMABSTRACTAUTO, 1)
                self.autoincrement = autoincrement
            self.writereg(DMDATA1, addr)
            self.writereg(DMCOMMAND, 0x00241000)
            self.statetag = 'RDSQ'
            self.stateval = addr
            self.waitopdone()
        if self.autoincrement:
            self.stateval += 4
        data = self.readreg(DMDATA0)
        if self.stateval == CH_RAM_BASE + CH_RAM_SIZE:
            self.waitopdone()
        return data

    # Read data blob from memory
    def readbinaryblob(self, addr, count):
        blob = b''
        while count:
            if ((addr & 3) == 0) and (count >= 4):
                data   = self.readword(addr)
                blob  += data.to_bytes(4, byteorder='little')
                addr  += 4
                count -= 4
            elif (addr & 1) or (count == 1):
                data   = self.readbyte(addr)
                blob  += data.to_bytes(1)
                addr  += 1
                count -= 1
            else:
                data   = self.readhalfword(addr)
                blob  += data.to_bytes(2, byteorder='little')
                addr  += 2
                count -= 2
        return blob

    # Write data blob to flash, 64-byte aligned
    def writebinaryblob(self, addr, data):
        if addr & 63:
            raise Exception('Blob is not 64-byte aligned')
        data = self.pad_data(data, 64)
        pages = self.page_data(data, 64)
        for page in pages:
            base = addr
            self.erasepage(base)
            self.writeword(0x40022010, 1<<16)
            self.writeword(0x40022010, (1<<16) | (1<<19))
            self.waitforflash()
            for x in range(0, 64, 4):
                value32 = int.from_bytes(page[x:x+4], byteorder='little')
                self.writeword(addr, value32)
                self.writeword(0x40022010, (1<<16) | (1<<18))
                self.waitforflash()
                addr += 4
            self.writeword(0x40022014, base)
            self.writeword(0x40022010,  (1<<16) | (1<<6))
            self.waitforflash()
            self.writeword(0x40022010, 0)

    #--------------------------------------------------------------

    # Pad data so that there are full pages
    def pad_data(self, data, pagesize):
        if (len(data) % pagesize) == 0:
            return data
        else:
            return data + b'\xff' * (pagesize - (len(data) % pagesize))

    # Divide data into pages
    def page_data(self, data, size):
        total_length = len(data)
        result = list()
        while len(result) < total_length / size:
            result.append(data[:size])
            data = data[size:]
        return result

    # Write data to code flash
    def flash_data(self, data):
        if len(data) > CH_CODE_SIZE:
            raise Exception('Not enough memory')
        self.writebinaryblob(CH_CODE_BASE, data)

    # Verify data in code flash
    def verify_data(self, data):
        dump = self.readbinaryblob(CH_CODE_BASE, len(data))
        if dump != data:
            raise Exception('Verification failed')


# ===================================================================================
# Debug Protocol Constants
# ===================================================================================

# USB device settings
CH_VENDOR_ID    = 0x1A86    # VID
CH_PRODUCT_ID   = 0x8010    # PID
CH_PACKET_SIZE  = 1024      # packet size
CH_INTERFACE    = 0         # interface number
CH_EP_OUT       = 0x01      # endpoint for data transfer out
CH_EP_IN        = 0x81      # endpoint for data transfer in
CH_TIMEOUT      = 5000      # timeout for USB operations

# Debug registers
DMDATA0         = 0x04
DMDATA1         = 0x05
DMCONTROL       = 0x10
DMSTATUS        = 0x11
DMHARTINFO      = 0x12
DMABSTRACTCS    = 0x16
DMCOMMAND       = 0x17
DMABSTRACTAUTO  = 0x18
DMPROGBUF0      = 0x20
DMPROGBUF1      = 0x21
DMPROGBUF2      = 0x22
DMPROGBUF3      = 0x23
DMPROGBUF4      = 0x24
DMPROGBUF5      = 0x25
DMPROGBUF6      = 0x26
DMPROGBUF7      = 0x27

DMCPBR          = 0x7C
DMCFGR          = 0x7D
DMSHDWCFGR      = 0x7E

# Memory constants
CH_RAM_BASE     = 0x20000000
CH_RAM_SIZE     = 2048
CH_CODE_BASE    = 0x08000000
CH_CODE_SIZE    = 16384
CB_BOOT_BASE    = 0x1FFFF00

# ===================================================================================

if __name__ == "__main__":
    _main()
