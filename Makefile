BINDIR = $(INSTPREFIX)/usr/bin

BIN_PROGS = plcm-daemon plcm-initramfs-module
PROGS = $(BIN_PROGS)

plcm_SRCS = plcm-daemon.c
plcm_OBJS = $(plcm_SRCS:.c=.o)

plcminitramfs_SRCS = plcm-initramfs-module.c
plcminitramfs_OBJS = $(plcminitramfs_SRCS:.c=.o)

CFLAGS += -Wall -D_GNU_SOURCE

all: $(PROGS)

plcm-daemon: $(plcm_OBJS)

plcm-initramfs-module : $(plcminitramfs_OBJS)

install :
	install -d ${DESTDIR}/${bindir}
	if [ -f plcm-initramfs-module ]; then install -m 0755 plcm-initramfs-module ${DESTDIR}/${bindir}/plcm-initramfs-module; fi
	if [ -f plcm-daemon ]; then install -m 0755 plcm-daemon ${DESTDIR}/${bindir}/plcm-daemon; fi

clean:
	$(RM) *.o
	if [ -f plcm-initramfs-module ]; then rm plcm-initramfs-module; fi
	if [ -f plcm-daemon ]; then rm plcm-daemon; fi

