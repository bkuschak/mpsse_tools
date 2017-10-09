This is a command-line version of a generic I2C read / write / scanning tool for use with 
FTDI MPSSE devices, such as this one:

https://www.digikey.com/product-detail/en/ftdi-future-technology-devices-international-ltd/C232HM-DDHSL-0/768-1106-ND/2714139

GND => Black => GND  
SCL => Orange  
SDA => Yellow + Green connected together  

Tested on Windows 10, but hopefully will work on MacOS and Linux also.

Requires the [FTDI 2xxx driver](http://www.ftdichip.com/FTDrivers.htm) to be installed first.

## Building

Windows:
I installed MinGW-w64 and make-3.82.  Just run make and it should build.

## Running
```
c:\git\mpsse_tools>i2c -h

Usage: i2c [-v14sS] [-d <name>] -a <addr> [-w] [-r <nbytes>] [b0 b1 ...>]
  -v:      increase verbosity (can be added multple times)
  -a:      7-bit slave address
  -w:      write to slave (data bytes must go at end)
  -r:      read <nbytes> from slave
  -1:      100 KHz clockrate
  -4:      400 KHz clockrate
  -S:      scan for FTDI devices and display serial numbers
  -d:      use a specific FTDI device serial number
  -s:      scan for I2C slaves

Three modes are supported:  read-only, write-only, and write-then-read.
Data can be in hex, binary, or octal format.
If a -d option is not given, we use the first FTDI device found

Read 1 byte from slave address 0x3C:
  i2c -a 0x3C -r 1

Write 4 bytes 0x12 0x34 0x56 0x78 to slave address 0x3C
  i2c -a 0x3C -w 0x12 0x34 0x56 0x78

Write 0x00 to slave address 0x3C, followed by repeated start and read of 4 bytes:
  i2c -a 0x3C -w -r 4 0x00

```

And doing something useful:

```
c:\git\mpsse_tools>i2c -s
Slave ACKed address 3c

c:\git\mpsse_tools>i2c -a 0x3C -r 2 -w 0x00
43 00
```

## Credits
This code started life as an adaptation of Tobias Muller's lm75.c from [twam.info](https://www.twam.info/hardware/i%C2%B2c-via-usb-on-os-x-using-ft232h)
