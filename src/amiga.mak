# Amiga Makefile.
#    (It's really just the Unix makefile with one or two small changes)

CC = gcc
CPU = 000

SP_REG = d6
PC_REG = d7

CODE_MODEL = -fbaserel 
#-ffixed-a3
CFLAGS = -g -Wall -fno-tree-loop-distribute-patterns -Dxxxx=-funroll-loops  -mregparm -m68$(CPU) -O3 -fomit-frame-pointer $(CODE_MODEL) -c -DPOSIX -DAMIGA -DLOUSY_RANDOM -DHARD_COLORS

# - free (1261) -fno-function-cse (1123) -fno-inline 
LDFLAGS = -g -mcrt=nix13 $(CODE_MODEL)
LIBS = /opt/amiga/m68k-amigaos/libnix/lib/libb/swapstack.o

INC = ztypes.h
OBJS = jzip.o control.o extern.o fileio.o input.o interpre.o license.o math.o \
	memory.o object.o operand.o osdepend.o property.o quetzal.o screen.o \
	text.o variable.o amigaio.o getopt.o

ifneq ($(PC_REG),)
CODE_MODEL += -ffixed-$(PC_REG)
CFLAGS += -DPC_REG=$(PC_REG)
endif

ifneq ($(SP_REG),)
CODE_MODEL += -ffixed-$(SP_REG)
CFLAGS += -DSP_REG=$(SP_REG)
endif


all: jzip

jzexe : jzexe.h
	$(CC) -o $@ $(LDFLAGS) $(OBJS)

jzip : $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $(OBJS) $(LIBS)
	
.phony: %.s
%.s: %.c %.o $(INC)
	$(CC) -S -o $@ $< $(CFLAGS) -fverbose-asm
	less $@

$(OBJS): $(INC) extern.c amiga.mak

clean :
	-rm *.o jzip
