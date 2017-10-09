all: i2c

i2c: i2c.c
	gcc -I. -Lwin64 -static -static-libgcc -o i2c i2c.c -lftd2xx
