VERSION=0.1
CFLAGS=-Wall -Wno-missing-braces -I. -g
KFLAGS=-D_KERNEL -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nodefaultlibs -g
KLDFLAGS=-r -dy -N misc/coretemp
LDFLAGS=-lkstat

GCC=gcc
LD=ld
CTFCONVERT=ctfconvert
CTFMERGE=ctfmerge

all: coretemp coretempstat

coretemp: coretemp.c coretemp.h
	$(GCC) $(CFLAGS) $(KFLAGS) -c coretemp.c -o coretemp.o
	$(CTFCONVERT) -i -L VERSION coretemp.o
	$(LD) $(KLDFLAGS) coretemp.o -o coretemp
	$(CTFMERGE) -L VERSION -o coretemp coretemp.o

coretempstat: coretempstat.c
	gcc $(CFLAGS) -std=gnu99 -o coretempstat $(LDFLAGS) coretempstat.c

clean: 
	rm -f coretemp.o coretemp coretempstat

install: all
	cp coretemp /kernel/drv/amd64/
	cp coretemp.conf /kernel/drv/
	rem_drv coretemp || true
	add_drv -m '* 0660 root sys' coretemp
