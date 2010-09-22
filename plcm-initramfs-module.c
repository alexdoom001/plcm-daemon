#include <sys/file.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "plcm_ioctl.h"

static int write_checksumm_error (int devfd) {
	char first_line[DSPL_WIDTH + 1] = {};
	char second_line[DSPL_WIDTH + 1] = {};

	snprintf (first_line, sizeof(first_line), "Status: Error");
	write(devfd, first_line, DSPL_WIDTH);

	snprintf (second_line, sizeof(second_line), "CRC check failed");

	write(devfd, second_line, DSPL_WIDTH);
	ioctl(devfd, PLCM_IOCTL_RESET_CGRAM, 0);
	return 0;
}

int main(int argc, char *argv[])
{
	int devfd;
	char status_message[DSPL_WIDTH + 1];

	devfd = open ("/dev/plcm_drv", O_RDWR);
	if(devfd == -1)
	{
		printf("Can't open /dev/plcm_drv\n");
		return -1;
	}

	ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
	ioctl(devfd, PLCM_IOCTL_CLEARDISPLAY, 0);
        ioctl(devfd, PLCM_IOCTL_RETURNHOME, 0);

	if (argc > 1 && strcmp ("checksum-error", argv[1])) {
		write_checksumm_error (devfd);
		goto out;	
	} 

	snprintf (status_message, sizeof(status_message), "Status: Loading");

        write(devfd, status_message, DSPL_WIDTH);

	ioctl(devfd, PLCM_IOCTL_RESET_CGRAM, 0);

out:
	ioctl(devfd, PLCM_IOCTL_DISPLAY_D, 1);
	ioctl(devfd, PLCM_IOCTL_DISPLAY_B, 0);
	ioctl(devfd, PLCM_IOCTL_DISPLAY_C, 0);
        close(devfd);
	return 0;
}
