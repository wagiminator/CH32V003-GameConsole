# rvprog.py - WCH-LinkE Programming Tool
## Description
This tool enables programming of WCH RISC-V microcontrollers (CH32Vxxx and CH32Xxxx) through their serial debug interface using [WCH-LinkE](http://www.wch-ic.com/products/WCH-Link.html) (please note the "E" in the name).

## Preparations
In order for this tools to work, Python3 must be installed on your system. To do this, follow these [instructions](https://www.pythontutorial.net/getting-started/install-python/). In addition [PyUSB](https://github.com/pyusb/pyusb) must be installed. On Linux (Debian-based), all of this can be done with the following commands:

```
sudo apt install python3 python3-pip
python3 -m pip install pyusb
```

Windows users in particular may also need to install [libusb](https://github.com/libusb/libusb).

To use the WCH-LinkE on Linux, you need to grant access permissions beforehand by executing the following commands:
```
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8010", MODE="666"' | sudo tee /etc/udev/rules.d/99-WCH-LinkE.rules
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8012", MODE="666"' | sudo tee -a /etc/udev/rules.d/99-WCH-LinkE.rules
sudo udevadm control --reload-rules
```

On Windows, if you need to you can install the WinUSB driver over the WCH interface 1.

## Usage
To upload firmware, you should make the following connections to the WCH-LinkE:

```
WCH-LinkE      CH32V003
+-------+      +------+
|  SWDIO| <--> |SWDIO |
|    GND| ---> |GND   |
|    3V3| ---> |VDD   |
+-------+      +------+
```

If the blue LED on the WCH-LinkE remains illuminated once it is connected to the USB port, it means that the device is currently in ARM mode and must be switched to RISC-V mode initially. There are a few ways to accomplish this:
- You can utilize the rvprog.py tool with the -v option (see below).
- Alternatively, you can select "WCH-LinkRV" in the software provided by WCH, such as MounRiver Studio or WCH-LinkUtility.
- Another option is to hold down the ModeS button on the device while plugging it into the USB port.

More information can be found in the [WCH-Link User Manual](http://www.wch-ic.com/downloads/WCH-LinkUserManual_PDF.html).

```
Usage: rvprog.py [-h] [-a] [-v] [-b] [-u] [-l] [-e] [-G] [-R] [-f FLASH]

Optional arguments:
  -h, --help                show help message and exit
  -a, --armmode             switch WCH-Link to ARM mode
  -v, --rvmode              switch WCH-Link to RISC-V mode
  -b, --unbrick             unbrick chip (CH32V003 only)
  -u, --unlock              unlock chip (remove read protection)
  -l, --lock                lock chip (set read protection)
  -e, --erase               perform a whole chip erase
  -G, --pingpio             make nRST pin a GPIO pin (CH32V003 only)
  -R, --pinreset            make nRST pin a reset pin (CH32V003 only)
  -f FLASH, --flash FLASH   write BIN file to flash

Example:
python3 rvprog.py -f firmware.bin
```

## Alternative Software Tools
- [WCH-LinkUtility](https://www.wch.cn/downloads/WCH-LinkUtility_ZIP.html)
- [minichlink](https://github.com/cnlohr/ch32v003fun/tree/master/minichlink)
- [wlink](https://github.com/ch32-rs/wlink)
