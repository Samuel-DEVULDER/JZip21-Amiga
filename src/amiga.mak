# Amiga Makefile.
#    (It's really just the Unix makefile with one or two small changes)

CC = gcc
CFLAGS =  -g -Wall -mregparm -m68000 -O3 -fomit-frame-pointer -c -DPOSIX -DAMIGA -DLOUSY_RANDOM -DHARD_COLORS
LDFLAGS = -g
LIBS = 

INC = ztypes.h
OBJS = jzip.o control.o extern.o fileio.o input.o interpre.o license.o math.o \
	memory.o object.o operand.o osdepend.o property.o quetzal.o screen.o \
	text.o variable.o amigaio.o getopt.o

all: jzip jzexe

jzexe : jzexe.h
	$(CC) -o $@ $(LDFLAGS) $(OBJS)

jzip : $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $(OBJS) $(LIBS)

$(OBJS): $(INC) extern.c amiga.mak

clean :
	-rm *.o jzip jzexe
