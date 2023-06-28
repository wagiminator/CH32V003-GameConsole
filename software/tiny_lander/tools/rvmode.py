#!/usr/bin/env python3
# ===================================================================================
# Project:   rvmode - Tool for switching WCH-LinkE to RISC-V mode (LinkRV mode)
# Version:   v1.0
# Year:      2023
# Author:    Stefan Wagner
# Github:    https://github.com/wagiminator
# License:   MIT License
# ===================================================================================
#
# Description:
# ------------
# Simple Python tool for switching the WCH-LinkE to RISC-V mode.
#
# Dependencies:
# -------------
# - PyUSB
#
# Operating Instructions:
# -----------------------
# You need to install PyUSB to use rvmode. Install it via "python -m pip install pyusb".
#
# Linux users need permission to access the device. Run:
# echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8010", MODE="666"' | sudo tee /etc/udev/rules.d/99-WCH-LinkE.rules
# echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8012", MODE="666"' | sudo tee -a /etc/udev/rules.d/99-WCH-LinkE.rules
# sudo udevadm control --reload-rules
#
# Connect the WCH-LinkE to your PC. If the blue LED on the device is on, run:
# python rvmode.py


import usb.core
import usb.util
import sys
import time

# USB device settings
CH_VENDOR_ID    = 0x1A86    # VID
CH_PRODUCT_ID   = 0x8010    # PID in RISC-V mode
CH_PRODUCT_ID2  = 0x8012    # PID in ARM mode
CH_EP_OUT       = 0x02      # endpoint for data transfer out
CH_EP_IN        = 0x83      # endpoint for data transfer in
CH_TIMEOUT      = 5000      # timeout for USB operations

# Connect to WCH-LinkE
print('Searching for WCH-Link in ARM mode ...')
armlink = usb.core.find(idVendor = CH_VENDOR_ID, idProduct = CH_PRODUCT_ID2)
if armlink is None:
    print('ERROR: No WCH-Link in ARM mode found!')
    sys.exit(1)
print('SUCCESS: Connected to WCH-Link in ARM mode.')

# Send mode switch command
try:
    print('Switching WCH-Link to RISC-V mode ...')
    armlink.write(CH_EP_OUT, b'\x81\xff\x01\x52')
except:
    print('ERROR: Failed to access WCH-Link!')
    sys.exit(1)

# Check if switching was successful
counter = 30
while counter:
    time.sleep(0.1)
    counter -= 1
    rvlink = usb.core.find(idVendor = CH_VENDOR_ID, idProduct = CH_PRODUCT_ID)
    if rvlink is not None:
        print('SUCCESS: WCH-Link is now in RISC-V mode.')
        sys.exit(0)

print('ERROR: Switching to RISC-V mode unsuccessful!')
sys.exit(1)
