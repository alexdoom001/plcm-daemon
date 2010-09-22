default : plcm-daemon

clean :
	rm plcm-daemon
	rm *.o

plcm-daemon : plcm-daemon.o 
	$(CC) $(LDLAGS) plcm-daemon.o -o plcm-daemon

plcm-daemon.o: plcm-daemon.c plcm_ioctl.h
	$(CC) $(CFLAGS) -c -o plcm-daemon.o plcm-daemon.c
	

install :
	cp plcm-daemon ${INSTALL_DIR}

