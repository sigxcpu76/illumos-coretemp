CFLAGS=-Wall -Wno-missing-braces -std=gnu99
KFLAGS=-D_KERNEL -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -nodefaultlibs
KLDFLAGS=-r
LDFLAGS=-lkstat

all: coretemp coretempstat

coretemp.o: coretemp.c
	gcc $(CFLAGS) $(KFLAGS) -c coretemp.c
coretemp: coretemp.o
	ld $(KLDFLAGS) -o coretemp coretemp.o

coretempstat: coretempstat.c
	gcc $(CFLAGS) -o coretempstat $(LDFLAGS) coretempstat.c
clean: 
	rm -f coretemp.o coretemp coretempstat

install: all
	cp coretemp /kernel/drv/amd64/
	cp coretemp.conf /kernel/drv/
	rem_drv coretemp || true
	add_drv -m '* 0660 root sys' coretemp
