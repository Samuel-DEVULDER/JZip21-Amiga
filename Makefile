OBJDIR = objs
MKDIR  = mkdir

ifneq ($(FROM_OBJDIR),1)

all: $(addprefix $(OBJDIR)/,jzip jzexe)
	-@cp  $^ .

%: $(OBJDIR)/%
	
$(OBJDIR)/%: $(OBJDIR)
	$(MAKE) --no-print-directory -f ../Makefile -C $(OBJDIR) FROM_OBJDIR=1 $*
	
$(OBJDIR):
	-$(MKDIR) $(OBJDIR)
	
else

Makefile: all

VPATH = ../src
include ../src/amiga.mak

CC = /opt/amiga/bin/m68k-amigaos-gcc
LDFLAGS = -s -mcrt=nix13
#-noixemul

endif