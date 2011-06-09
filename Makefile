# $Id: Makefile 8169 2011-01-07 17:32:36Z luigi $

#CC=/home/tyler/Projects/Kindle/arm-2008q3/bin/arm-none-linux-gnueabi-gcc
CC=/usr/bin/gcc
#STRIP=/home/tyler/Projects/Kindle/arm-2008q3/bin/arm-none-linux-gnueabi-strip
STRIP=/usr/bin/strip
CFLAGS = -O1 -Wall -Werror -g 
# files to publish
PUB= $(HEADERS) $(ALLSRCS) ajaxterm.* Makefile README myts.arm launchpad.ini keydefs.ini

HEADERS = config.h dynstring.h font.h myts.h pixop.h screen.h terminal.h
HEADERS += linux/
ALLSRCS= myts.c terminal.c dynstring.c cp437.c
ALLSRCS += config.c launchpad.c
ALLSRCS += screen.c pixop.c
# ALLSRCS += sip.c
SPLIT=1
ifeq ($(SPLIT),)
SRCS= myts.c
else
SRCS= $(ALLSRCS)
CFLAGS += -DSPLIT
endif
CFLAGS += -I.

OBJS := $(strip $(patsubst %.c,%.o,$(strip $(SRCS))))

myts.arm: $(OBJS)
	$(CC) $(CFLAGS) -o myts.arm $(OBJS) -lutil

$(OBJS): myts.h
terminal.o: terminal.h

tgz: $(PUB)
	tar cvzf /tmp/kiterm.tgz --exclude .svn $(PUB)

clean:
	rm -rf myts.arm *.o *.core

# conversion
# hexdump -e '"\n\t" 8/1 "%3d, "'
# DO NOT DELETE
