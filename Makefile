all: i2c

i2c: i2c.c
	gcc -I. -Lwin64 -o i2c i2c.c -lftd2xx
