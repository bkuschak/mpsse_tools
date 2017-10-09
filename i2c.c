/***************************************************************************
 *   i2c Version 0.1                                                       *
 *   Using FTDI 2xxx driver interface, bit bang I2C master mode.           *
 *   Does not use libftdi nor libmpsse-i2c                                 *
 *   10/8/2017, B. Kuschak <bkuschak@yahoo.com>                            *
 *                                                                         *
 *   Adapted from:                                                         *
 *   lm75 Version 0.1                                                      *
 *   Copyright (C) 2011 by Tobias Müller                                   *
 *   Tobias_Mueller@twam.info                                              *
 *                                                                         *
 *   To build use the following gcc statement                              *
 *   gcc -o i2c i2c.c -lftd2xx                                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

// Tested on Windows 10 using MinGW-w64, FTDI CDM v2.12.28 driver, and C232HM-DDHSL-0 cable

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ftd2xx.h>
#include <getopt.h>
#include <string.h>
#include <string.h>

#define MAX_DEVICES		16

#define READ(addr)		(((addr)<<1) | 0x01)
#define WRITE(addr)		(((addr)<<1) | 0x00)

#define SLAVE_NAK_ADDR		-2
#define SLAVE_NAK_DATA		-3


// A few comments about how the FTDI chips work:
// The FTDI has a command buffer that is loaded with one or more commands, then sent for execution.
// The command protocol is little endian.
// Each pin is only input or only output.  SDA is therefore connected to two pins, TDI and TDO.
// Some of our code bit bangs the lines, to generate the start and stop conditions.  Other code uses clocked data 
// and ACK/NAK bits.  
// Only the FT232H has open drain capability.  Others need to be handled differently - TBD
//
// For those devices that use cables, keep in mind there will be crosstalk on those cables and the signal integrity
// might be very poor.  Consider using very short cables, and/or separation between SDA and SCL.
//
// Protocol references:
// http://www.ftdichip.com/Support/Documents/AppNotes/AN_135_MPSSE_Basics.pdf
// http://www.ftdichip.com/Support/Documents/AppNotes/AN_108_Command_Processor_for_MPSSE_and_MCU_Host_Bus_Emulation_Modes.pdf
// AN_113_FTDI_Hi_Speed_USB_To_I2C_Example.pdf
//

// C232HM-DDHSL-0 pinout:
// SCL: ADBUS0 (TCK)
// SDA: ADBUS1 (TDI) and 
//      ADBUS2 (TDO) tied together.
// This is for FT232H, maybe others also
#define SCL		(1<<0)
#define SDA_OUT		(1<<1) 
#define SDA_IN		(1<<2) 

// MPSSE opcodes
const unsigned char MSB_FALLING_EDGE_CLOCK_BYTE_OUT = 0x11;
const unsigned char MSB_FALLING_EDGE_CLOCK_BIT_OUT = 0x13;
const unsigned char MSB_FALLING_EDGE_CLOCK_BYTE_IN = 0x20;
const unsigned char MSB_RISING_EDGE_CLOCK_BIT_IN = 0x22;
const unsigned char SET_BITS_LOW_BYTE = 0x80;
const unsigned char SEND_IMMEDIATE = 0x87;
const unsigned char DISABLE_LOOPBACK = 0x85;
const unsigned char SET_CLK_DIV = 0x86;
const unsigned char DISABLE_CLK_DIV_5 = 0x8A;
const unsigned char ENABLE_3_PHASE_CLK = 0x8C;
const unsigned char DISABLE_ADAPTIVE_CLK = 0x97;
const unsigned char OPEN_DRAIN = 0x9E;			// FT232H only

int verbose;
int ftdi_scan;

FT_STATUS ftStatus;
FT_HANDLE ftHandle;

char inputBuffer[1024];
char outputBuffer[1024];
unsigned int outputSize;
long unsigned int outputSent;
long unsigned int inputSize;
long unsigned int inputRead;

char *default_serial_num;		// will be the first one we find

int scan_devices() {
	char* pcBufLD[MAX_DEVICES + 1];
	char cBufLD[MAX_DEVICES][64];
	int iNumDevs = 0;
	int i;

	for(i = 0; i < MAX_DEVICES; i++) {
		pcBufLD[i] = cBufLD[i];
	}
	pcBufLD[MAX_DEVICES] = NULL;

	FT_STATUS ftStatus = FT_ListDevices(pcBufLD, &iNumDevs, FT_LIST_ALL | FT_OPEN_BY_SERIAL_NUMBER);

	if (ftStatus != FT_OK) {
		fprintf(stderr, "Error: FT_ListDevices(%d)\n", ftStatus);
		return 1;
	}

	for (i = 0; ( (i <MAX_DEVICES) && (i < iNumDevs) ); i++) {
		if(verbose || ftdi_scan) 
			fprintf(stderr, "Device %d Serial Number - %s\n", i, cBufLD[i]);
		if(i == 0)
			default_serial_num = strdup(cBufLD[i]);
	}

	return 0;
}

// append a command or data value to the command buffer
int append(unsigned char data)
{
	if(outputSize >= sizeof(outputBuffer)-1) {
		fprintf(stderr, "Error: Command buffer full!\n");
		return -1;
	}
	outputBuffer[outputSize++] = data;
	return 0;
}

// empty the command buffer
void flush(void) 
{
	outputSize = 0;
}

// execute the command buffer
FT_STATUS execute(void) 
{
	if(outputSize == 0)
		return 0;

	ftStatus = FT_Write(ftHandle, outputBuffer, outputSize, &outputSent);
	outputSize = 0;
	return ftStatus;
}

// bk
int set_bits(unsigned char data)
{
#if 0
 	// FT2232H and FT4232H do not have open-drain capability
	// 
	// open drain: direction is only output when data value is low, otherwise it is an input (Hi-Z)
	// direction: 1=output,     0=input
	// state:     0=output low, 1=input (hi-Z)
	int dir = (~data) & 0xFF;		
	append(SET_BITS_LOW_BYTE);
	append(data);
	append(dir);
#else
	// Open drain is configured with 0x9E command for the FT232H.  Always drive SDA/SCL in open-drain mode.
	append(SET_BITS_LOW_BYTE);
	append(data);
	append(SDA_OUT|SCL);	
#endif
}

// start is a falling edge on SDA while SCL high
void i2c_start_bk(void) 
{
	int i;
	int repeat = 40;			// hack - repeat the command to ensure bit bang is slow enough

	// SDA high, SCL high			// bk - redundant?
	for (i=0; i<repeat; i++) 
		set_bits(SDA_OUT|SCL);

	// SDA low, SCL high
	for (i=0; i<repeat; i++) 
		set_bits(SCL);

	// SDA low, SCL low
	for (i=0; i<repeat; i++) 
		set_bits(0);
}

// stop is a rising edge on SDA while SCL high
void i2c_stop_bk(void) 
{
	int i;
	int repeat = 40;			// hack - repeat the command to ensure bit bang is slow enough

	// SDA low, SCL (already) low  
	for (i=0; i<repeat; i++) 
		set_bits(0);

	// SDA low, SCL high
	for (i=0; i<repeat; i++) 
		set_bits(SCL);

	// SDA high, SCL high
	for (i=0; i<repeat; i++) 
		set_bits(SDA_OUT|SCL);
}

// bk - should we make this take multiple bytes for efficiency? no because we have to check ACK on each byte
int i2c_send_bk(unsigned char data) 
{
	// clock data byte on clock edge MSB first
	// data length of 0x0000 means 1 byte
	append(MSB_FALLING_EDGE_CLOCK_BYTE_OUT);
	append(0x00); 	// LSB len
	append(0x00);	// MSB len
	append(data);

	// SDA tristate, SCL low
	set_bits(SDA_OUT);

	// clock data bit on clock edge 
	// length of 0x00 means scan 1 bit
	append(MSB_RISING_EDGE_CLOCK_BIT_IN);
	append(0x00); 		

	// flush buffer and return data to PC
	append(SEND_IMMEDIATE);

	// execute the queued commands
	execute();

	// read ACK/NAK
	ftStatus = FT_Read(ftHandle, inputBuffer, 1, &inputRead);
	//readback(1);

	if ((ftStatus != FT_OK) || (inputRead == 0)) {
		return -2;		// error
	}
	else if (((inputBuffer[0] & 0x01) != 0x00)) {
		return -1;		// NAK
	}
	else {
		// SDA high, SCL low
		set_bits(SDA_OUT);
		execute();
		return 0;		// success
	}
}

// read some number of bytes and ACK every one except the last
int read_bytes(int nbytes)
{
	int i;

	if(nbytes <= 0)				// fixme or > buffer size?
		return -1;

	for(i=0; i<nbytes; i++) {
		// data byte
		append(MSB_FALLING_EDGE_CLOCK_BYTE_IN);
		append(0x00);		// one byte
		append(0x00);

		// ACK/NAK 
		append(MSB_FALLING_EDGE_CLOCK_BIT_OUT);
		append(0x00); 		// scan 1 bit
		if (i == (nbytes-1)) {
			append(0x80); 	// master NAK	
			if(verbose > 1)
				fprintf(stderr, "master NAK read\n");
		}
		else {
			append(0x00);	// master ACK
			if(verbose > 1)
				fprintf(stderr, "master ACK read\n");
		}
	}

	// send answer back immediate
	append(SEND_IMMEDIATE);
	execute();

	ftStatus = FT_Read(ftHandle, inputBuffer, nbytes, &inputRead);

	return 0;
}

// dump received bytes
void dump_hex(unsigned char *buf, int len) 
{
	int i;
	for(i=0; i<len; i++) {
		fprintf(stdout, "%02hx ", buf[i]);
		if(((i+1) % 16) == 0) 
			fprintf(stdout, "\n");
	}
}

int ftdi_configure_i2c(char *serial_num, int speed_khz)
{

	unsigned short dwClockDivisor;

	if(speed_khz == 400) {
		dwClockDivisor = 0x004A;	// 400 KHz clock?
	}
	else {
		dwClockDivisor = 0x012B; 	// 100 kHz clock
	}

	// setup
	if((ftStatus = FT_OpenEx(serial_num, FT_OPEN_BY_SERIAL_NUMBER, &ftHandle)) != FT_OK){
		/*
			This can fail if the VCP driver is loaded!
			Linux: use lsmod to check this and rmmod ftdi_sio to remove also rmmod usbserial
			OS X: sudo kextunload /System/Library/Extensions/FTDIUSBSerialDriver.kext
	 	*/
		fprintf(stderr, "Error FT_OpenEx(%d)\n", ftStatus);
		fprintf(stderr, "Is the FTDI VCP driver loaded by chance? It may conflict with the 2XXX driver we use.\n");
		fprintf(stderr, "Linux: use lsmod to check this and rmmod ftdi_sio to remove also rmmod usbserial\n");
		fprintf(stderr, "OS X: sudo kextunload /System/Library/Extensions/FTDIUSBSerialDriver.kext\n");
		return -1;
	}

	// reset device
	ftStatus = FT_ResetDevice(ftHandle);

	// Purge USB receive buffer first by reading out all old data from FT2232H receive buffer
	ftStatus |= FT_GetQueueStatus(ftHandle, &inputSize);
	if ((ftStatus == FT_OK) && (inputSize > 0))
		FT_Read(ftHandle, &inputBuffer, inputSize, &inputRead);

	//Set USB request transfer size
	ftStatus |= FT_SetUSBParameters(ftHandle, 65536, 65535);

	//Disable event and error characters
	ftStatus |= FT_SetChars(ftHandle, 0, 0, 0, 0);

	//Sets the read and write timeouts in milliseconds for the FT2232H
	ftStatus |= FT_SetTimeouts(ftHandle, 0, 5000);

	//Set the latency timer
	ftStatus |= FT_SetLatencyTimer(ftHandle, 16);

	//Reset controller
	ftStatus |= FT_SetBitMode(ftHandle, 0x0, 0x00);

	// enable MPSSE mode, all inputs
	ftStatus |= FT_SetBitMode(ftHandle, 0x0, 0x02);

	if (ftStatus != FT_OK) {
		fprintf(stderr, "Error occured: %u\n", ftStatus);
		return -1;
	}

	// Disables the clk divide by 5 to allow for a 60MHz master clock.
	// FIXME - BK do we really want to do this with I2C?  
	append(DISABLE_CLK_DIV_5);

	// Disable adaptive clocking
	append(DISABLE_CLK_DIV_5);

	// Enables 3 phase data clocking. Used by I2C interfaces to allow data on both clock edges.
	append(ENABLE_3_PHASE_CLK);

	// send commands
	execute();

	// Set values and directions of lower 8 pins (ADBUS7-0)
	append(SET_BITS_LOW_BYTE);

	// Set SK,DO high
	append(SDA_OUT|SCL);

	// Set SK,DO as output, other as input
	append(SDA_OUT|SCL);

	// Set clock divisor
	append(SET_CLK_DIV);
	append(dwClockDivisor & 0xFF); 				// low byte
	append((dwClockDivisor >> 8) & 0xFF); 			// high byte

	// send commands
	execute();

	// The FT232H supports open-drain mode directly 
	//if(FT232H)
	append(OPEN_DRAIN);
	append(SDA_OUT|SCL);		// low byte enable
	append(0x00);			// high byte enable
	execute();

	// Turn off Loopback
	append(DISABLE_LOOPBACK);
	execute();

	return 0;
}

// Handle write-only, read-only, and write-then-read
int i2c_transaction(unsigned char slave_addr, int nread, int nwrite, unsigned char *wbuf)
{
	int i;

	if(nwrite) {
		if(verbose > 0)
			fprintf(stderr, "Writing %d bytes, slave addr 0x%02hx\n", nwrite, slave_addr);

		// send addr
		i2c_start_bk();
		if(verbose > 1)
			fprintf(stderr, "Start\n");
		if(i2c_send_bk(WRITE(slave_addr)) != 0) {
			// slave NAK
			i2c_stop_bk();
			execute();
			if(verbose > 1)
				fprintf(stderr, "Slave NAKed address\n");
			return SLAVE_NAK_ADDR;
		}
		else {
			if(verbose > 1)
				fprintf(stderr, "Slave ACKed address\n");
		}
		
		// send data
		for(i=0; i<nwrite; i++) {
			if(i2c_send_bk(wbuf[i]) != 0) {
				// slave NAK
				i2c_stop_bk();
				execute();
				if(verbose > 1)
					fprintf(stderr, "Slave NAKed write data\n");
				return SLAVE_NAK_DATA;
			}
			else {
				if(verbose > 1)
					fprintf(stderr, "Slave ACKed data\n");
			}
		}
	}
	if(nread) {
		if(verbose > 0)
			fprintf(stderr, "Reading %d bytes, slave addr 0x%02hx\n", nread, slave_addr);

		i2c_start_bk();		// start or repeated start
		if(verbose > 1)
			fprintf(stderr, "Start\n");

		if(i2c_send_bk(READ(slave_addr)) != 0) {
			// slave NAK
			i2c_stop_bk();
			execute();
			if(verbose > 1)
				fprintf(stderr, "Slave NAKed address\n");
			return SLAVE_NAK_ADDR;
		}
		else {
			if(verbose > 1)
				fprintf(stderr, "Slave ACKed address\n");
		}
		
		// read data
		// fixme if nread > max buffer size FTDI supports, we need to iterate here
		read_bytes(nread);

		// dump hex bytes, optionally as hexedit dump?
		// optionally write binary data only?
		// to stdout or file
		// fixme if we dump here we delay the stop bit...
		dump_hex(inputBuffer, inputRead);
	}
	if(nread || nwrite) {
		i2c_stop_bk();	
		execute();
		if(verbose > 1)
			fprintf(stderr, "Stop\n");
	}
	return 0;
}

void usage(char *s)
{
	fprintf(stdout, "Usage: %s [-v14sS] [-d <name>] [-f <filename>] -a <addr> [-w] [-r <nbytes>] [b0 b1 ...>]\n"
			"  -v:      increase verbosity (can be added multple times)\n"
			"  -a:      7-bit slave address\n"
			"  -w:      write to slave (data bytes must go at end)\n"
			"  -r:      read <nbytes> from slave\n"
			"  -1:      100 KHz clockrate\n"
			"  -4:      400 KHz clockrate\n"
			"  -S:      scan for FTDI devices and display serial numbers\n"
			"  -d:      use a specific FTDI device serial number\n"
			"  -s:      scan for I2C slaves\n"
			"  -f:      Use file for data (if read-only or write-read) or writing (if write-only)\n"
			"\n"
			"Three modes are supported:  read-only, write-only, and write-then-read.\n"
			"Data can be in hex, binary, or octal format.\n"
			"If a -d option is not given, we use the first FTDI device found\n"
			"\n"
			"Read 1 byte from slave address 0x3C:\n"
			"  %s -a 0x3C -r 1\n"
			"\n"
			"Write 4 bytes 0x12 0x34 0x56 0x78 to slave address 0x3C\n"
			"  %s -a 0x3C -w 0x12 0x34 0x56 0x78\n"
			"\n"
			"Write 0x00 to slave address 0x3C, followed by repeated start and read of 4 bytes:\n"
			"  %s -a 0x3C -w -r 4 0x00\n"
			"\n"
			"If using -f file, the file is used for storing read data or sourcing write data.  In the\n"
			"case of write-read mode, the write data must be supplied on the command line and file is\n"
			"used to store read data. FIXME - if write data provided on command line it is used first.\n"
			"This is useful for programming EEPROMs\n"
			"\n", s, s, s, s);
	exit(1);
}

// Windows has no strsep?
char* mystrsep(char** stringp, const char* delim)
{
	char* start = *stringp;
	char* p;

	p = (start != NULL) ? strpbrk(start, delim) : NULL;

	if (p == NULL) {
		*stringp = NULL;
	}
	else {
		*p = '\0';
		*stringp = p + 1;
	}
	return start;
}


int main(int argc, char** argv) {
	int i, c;
	int i2c_scan = 0;
	int writing = 0;
	int nread = 0;
	int nwrite = 0;
	int power_out = 0;
	int wbufsize = 128;		// grows if needed
	char *pwbuf = NULL;
	char *wbuf = NULL;
	char *serial_num = NULL;	// of the mpsse device
	int serial_num_idx = -1;
	int speed_khz = 100;
	int slave_addr = -1;
	char *fname = NULL;

	while((c = getopt(argc, argv, "?ha:vr:wSsd:14f:")) != EOF) {
		switch(c) {
			case '1':
				speed_khz = 100;
				break;
			case '4':
				speed_khz = 400;
				break;
			case 'a':
				// Address is always the 7-bit address (0x00 to 0x7F). None of this 8-bit nonsense.
				slave_addr = (unsigned char)strtoul(optarg, NULL, 0);
				slave_addr &= 0x7F;
				break;
			case 'S':
				ftdi_scan = 1;
				break;
			case 's':
				i2c_scan = 1;
				break;
			case 'w':
				writing = 1;
				break;
			case 'r':
				nread = strtoul(optarg, NULL, 0);
				break;
			case 'd':
				serial_num = optarg;
				break;
			case 'v':
				verbose++;
				break;
			case 'f':
				fname = optarg;		// FIXME
				break;
			case 'h':
			case '?':
			default: 
				usage(argv[0]);
		}
	}

	if(slave_addr < 0 && ftdi_scan == 0 && i2c_scan == 0)
		usage(argv[0]);

	// grab all remaining args as the data to write
	if(writing) {
		int len;
		pwbuf = wbuf = calloc(wbufsize, 1);
		while(optind < argc) {
			char *s, *arg = argv[optind++];
			// may be a single byte or a string of bytes in each argv
			// we accept "0x12 0x23 0x24"  or "18 35 36"
			while((s = mystrsep(&arg, " "))) {
				// grow our buffer if needed
				if((len = pwbuf-wbuf) >= wbufsize) {
					wbufsize *= 2;
					printf("Growing to %d bytes\n", wbufsize);
					wbuf = realloc(wbuf, wbufsize);
					pwbuf = wbuf + len;
				}
				*pwbuf++ = strtoul(s, NULL, 0);
			}
		}
	}
	nwrite = pwbuf - wbuf;


	// if scanning only just do that and exit
	if(ftdi_scan) {
		fprintf(stdout, "Scanning for MPSSE devices...\n");
		scan_devices();
		exit(0);
	}

	// If no serial number was given, just pick the first one
	if (!serial_num && serial_num_idx < 0) {
		scan_devices();
		serial_num = default_serial_num;
		if(verbose)
			fprintf(stderr, "Using %s\n", serial_num);
	}
	if(!serial_num) {
		fprintf(stderr, "Failed to find a suitable MPSSE device!\n");
		exit(-1);
	}

	// open the device and configure it
	if(ftdi_configure_i2c(serial_num, speed_khz) != 0) {
		fprintf(stderr, "Failed to configure the FTDI for I2C\n");
		FT_Close(ftHandle);
		exit(1);
	}

	// I2C bus scan
	if(i2c_scan) {
		for(i=0; i<0x7f; i++) {

			i2c_start_bk();
			if(verbose > 1)
				fprintf(stderr, "Start\n");
			if(i2c_send_bk(READ(i)) != 0) {
				if(verbose > 1)
					fprintf(stderr, "Slave NAKed address %02hx\n", (unsigned char)i);
			}
			else {
				fprintf(stderr, "Slave ACKed address %02hx\n", (unsigned char)i);
			}
			i2c_stop_bk();
			execute();
		}
	}

	i2c_transaction(slave_addr, nread, nwrite, wbuf);
	
	FT_Close(ftHandle);
	return 0;
}

// TODO:
// GPIO mode
// SPI mode
// maybe also a socket interface 
// maybe also a pattern generator, like a pulse generator or sequencer for generating complex patterns

