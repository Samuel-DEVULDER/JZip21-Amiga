OBJDIR = objs
MKDIR  = mkdir

OS:=$(shell uname)

ifeq ($(OS),)
BASEDIR :=
else
BASEDIR := ..
endif

ifneq ($(FROM_OBJDIR),1)

all: jzip

jzip: $(OBJDIR)/jzip

%: $(OBJDIR)
%: $(OBJDIR)/%
	
$(OBJDIR)/%:
	$(MAKE) --no-print-directory -f "$(BASEDIR)/Makefile" -C $(OBJDIR) FROM_OBJDIR=1 $*
	-cp $(OBJDIR)/jzip jzip
	
$(OBJDIR):
	-$(MKDIR) $(OBJDIR)
	
else

Makefile:

VPATH = $(BASEDIR)/src
include $(BASEDIR)/src/amiga.mak

CC = gcc
ifeq ($(patsubst MSYS%,WIN,$(patsubst CYGWIN%,WIN,$(OS))),WIN)
CC = /opt/amiga/bin/m68k-amigaos-gcc
endif

ifeq ($(PROFILE),1)
CFLAGS += -g -pg -fno-inline
CFLAGS := $(CFLAGS:-fomit-frame-pointer=)
LDFLAGS += -g -pg 
endif

ifeq ($(OS),AmigaOS)
CFLAGS +=
LIBS += -L gg:lib/libnix -lstubs
# CODE_MODEL = -fbaserel
endif
#-noixemul

endif