# WCH-LinkE Programming Tools
## WCH-LinkE
To program the CH32V003 microcontroller, you will need a special programming device which utilizes the proprietary single-wire serial debug interface (SDI). The [WCH-LinkE](http://www.wch-ic.com/products/WCH-Link.html) (pay attention to the "E" in the name) is a suitable device for this purpose and can be purchased commercially for around $3. This debugging tool is not only compatible with the CH32V003 but also with other WCH RISC-V and ARM-based microcontrollers.

To use the WCH-Link on Linux, you need to grant access permissions beforehand by executing the following commands:
```
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8010", MODE="666"' | sudo tee /etc/udev/rules.d/99-WCH-LinkE.rules
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8012", MODE="666"' | sudo tee -a /etc/udev/rules.d/99-WCH-LinkE.rules
sudo udevadm control --reload-rules
```

On Windows, if you need to you can install the WinUSB driver over the WCH interface 1.

To upload firmware, you should make the following connections to the WCH-LinkE:

```
WCH-LinkE      MCU Board
+-------+      +-------+
|  SWDIO| <--> |DIO    |
|    GND| ---> |GND    |
|    3V3| ---> |3V3    |
+-------+      +-------+
```

If the blue LED on the WCH-LinkE remains illuminated once it is connected to the USB port, it means that the device is currently in ARM mode and must be switched to RISC-V mode initially. There are a few ways to accomplish this:
- You can utilize the Python tool called rvmode.py (refer to the instructions below).
- Alternatively, you can select "WCH-LinkRV" in the software provided by WCH, such as MounRiver Studio or WCH-LinkUtility.
- Another option is to hold down the ModeS button on the device while plugging it into the USB port.

More information can be found in the [WCH-Link User Manual](http://www.wch-ic.com/downloads/WCH-LinkUserManual_PDF.html).

## WCH-LinkUtility
WCH offers the free but closed-source software [WCH-LinkUtility](https://www.wch.cn/downloads/WCH-LinkUtility_ZIP.html) to upload firmware with Windows.

## minichlink
Minichlink by CNLohr provides a free, open mechanism to use the WCH-LinkE programming dongle for the CH32V003. It works with Windows, Linux and Mac. The x86-64 Linux binary is provided in this folder, the source code can by found on the [developer's Github page](https://github.com/cnlohr/ch32v003fun/tree/master/minichlink).

```
Usage: minichlink [args]
 single-letter args may be combined, i.e. -3r
 multi-part args cannot.
 -3 Enable 3.3V
 -5 Enable 5V
 -t Disable 3.3V
 -f Disable 5V
 -u Clear all code flash - by power off (also can unbrick)
 -b Reboot out of Halt
 -e Resume from halt
 -a Place into Halt
 -D Configure NRST as GPIO
 -d Configure NRST as NRST
 -s [debug register] [value]
 -g [debug register]
 -w [binary image to write] [address, decimal or 0x, try0x08000000]
 -r [output binary image] [memory address, decimal or 0x, try 0x08000000] [size, decimal or 0x, try 16384]
   Note: for memory addresses, you can use 'flash' 'launcher' 'bootloader' 'option' 'ram' and say "ram+0x10" for instance
   For filename, you can use - for raw or + for hex.
 -T is a terminal. This MUST be the last argument.

Example:
minichlink -w firmware.bin flash -b
```

## rvprog.py
You can also use rvprog.py, a simple Python tool provided in this folder, to flash CH32V003 microcontrollers using the WCH-LinkE or compatible programmers/debuggers. The code for this tool is largely derived from CNLohr's minichlink.

In order for this tool to work, Python3 must be installed on your system. To do this, follow these [instructions](https://www.pythontutorial.net/getting-started/install-python/). In addition [PyUSB](https://github.com/pyusb/pyusb) must be installed. On Linux (Debian-based), all of this can be done with the following commands:

```
sudo apt install python3 python3-pip
python3 -m pip install pyusb
```

```
Usage: rvprog.py [-h] [-a] [-v] [-b] [-u] [-l] [-e] [-G] [-R] [-f FLASH]

Optional arguments:
  -h, --help                show help message and exit
  -a, --armmode             switch WCH-Link to ARM mode
  -v, --rvmode              switch WCH-Link to RISC-V mode
  -b, --unbrick             unbrick chip
  -u, --unlock              unlock chip (remove read protection)
  -l, --lock                lock chip (set read protection)
  -e, --erase               perform a whole chip erase
  -G, --pingpio             make nRST pin a GPIO pin
  -R, --pinreset            make nRST pin a reset pin
  -f FLASH, --flash FLASH   write BIN file to flash

Example:
python3 rvprog.py -f firmware.bin
```

## rvmode.py
The Python tool rvmode.py, which can be found in this folder, provides a simple way to switch the WCH-LinkE to RISC-V mode (LinkRV mode). Python3 and PyUSB must be installed on your system for the tool to work.
```
Usage example:
python3 rvmode.py
```
