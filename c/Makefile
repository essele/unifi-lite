
LUADIR				= /home/essele/dev/unifi/luajit-2.0/src

CFLAGS				= -I$(LUADIR)
LDFLAGS				= -L$(LUADIR)

ALL_D				:= $(wildcard *.d)

%.so:	%.o
	@echo [LD] $@
	@$(CC) -shared -o $@ $^ $(CFLAGS) $(CFLAGS_$@) $(LDFLAGS) $(LDFLAGS_$@) 

%.o:	%.c Makefile
	@echo [CC] $@
	@$(CC) -MD -c -Wall -Werror -fpic -o $@ $(CFLAGS) $(CFLAGS_$(*).c) $<

#
# All of the LIBS we wan to build
#
LIBS				= unisvr.so

all:				$(LIBS)

clean:
	rm *.o *.so *.d

#
# Which objects to each of the libs need...
#
unisvr.so:			unisvr.o serialise.o

#
# Special Flags
#
LDFLAGS_unisvr.so = -lssl

include $(ALL_D)

