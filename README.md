This is a command-line version of a generic I2C read / write / scanning tool for use with 
FTDI MPSSE devices, such as this one:

https://www.digikey.com/product-detail/en/ftdi-future-technology-devices-international-ltd/C232HM-DDHSL-0/768-1106-ND/2714139

Black => GND
Orange => SCL
Yellow + Green connected together => SDA

Tested on Windows 10, but hopefully will work on MacOS and Linux also.

Requires the [FTDI 2xxx driver](http://www.ftdichip.com/FTDrivers.htm) to be installed first.

## Building

Windows:
I installed MinGW-w64 and make-3.82.  Just run make and it should build.

## Credits
This code started life as an adaptation of Tobias Muller's lm75.c from [twam.info](https://www.twam.info/hardware/i%C2%B2c-via-usb-on-os-x-using-ft232h)
