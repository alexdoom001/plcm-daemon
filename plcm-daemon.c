/*
 * Lanner Paralle LCM Driver Test Program
 */
#include <sys/file.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/if_bonding.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "plcm_ioctl.h"


#define DAEMON_NAME "plcm-daemon"
# define FSHIFT 16
#define FIXED_1         (1<<FSHIFT) 
#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

#define KEYPAD_UP 0xC7
#define KEYPAD_DOWN 0xCF
#define KEYPAD_LEFT 0xE7
#define KEYPAD_RIGHT 0xEF

#define INTERFACE_NAME_LEN 10
#define INTERFACE_MAX_COUNT 130

#define STATUS_FILE "/var/log/plcm.status"

typedef enum {
	DISPLAY_A = 0,
	DISPLAY_B,
	DISPLAY_C,
	DISPLAY_ETH,
	DISPLAY_ETH_ADDR,
	DISPLAY_START
} DISPLAY_TYPE;


typedef enum {
	COMMIT_UNINIT = -1,
	COMMIT_SUCCESS = 0,
	COMMIT_FAIL
} COMMIT_STATUS_TYPE;

static DISPLAY_TYPE current_display = DISPLAY_START;
static int devfd = 0;
static int network_interface_index = 0;
static int network_interface_count = 0;
static char network_interface_array[INTERFACE_MAX_COUNT][INTERFACE_NAME_LEN];
static int processor_count = 0;
static COMMIT_STATUS_TYPE commit_status = COMMIT_UNINIT;
static int commit_status_changed = 0;

static void check_commit_status();

static void write_display_a () {
	int loadavg = 0;
	char str_loadavg[DSPL_WIDTH + 1] = {};
	struct sysinfo info;

	check_commit_status();
	if (current_display != DISPLAY_A || commit_status_changed) {
		char status_message[DSPL_WIDTH + 1] = {};

		ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
	        ioctl(devfd, PLCM_IOCTL_RETURNHOME, 0);

		if (commit_status == COMMIT_SUCCESS) {
			snprintf(status_message, sizeof (status_message), "Status: OK", 1, 1);
		} else {
			snprintf(status_message, sizeof (status_message), "Status: Error", 1, 1);
		}

        	write(devfd, status_message, sizeof(status_message));
		ioctl(devfd, PLCM_IOCTL_RESET_CGRAM, 0);
	}

	ioctl(devfd, PLCM_IOCTL_SET_LINE, 2);
        ioctl(devfd, PLCM_IOCTL_RETURNHOME, 0);

	sysinfo(&info);
	loadavg = (LOAD_INT(info.loads[1]) * 100 + LOAD_FRAC(info.loads[1]) ) / processor_count;

	snprintf(str_loadavg, sizeof(str_loadavg), "Load: %d%%", 2, 3, 4, 5, loadavg);
	write(devfd, str_loadavg, DSPL_WIDTH);

	current_display = DISPLAY_A;
	alarm (10);
	return; 
}

static int write_display_b () {
	char disk_stat[DSPL_WIDTH + 1] = {};
	char root_use[4] = {};
	char var_use[4] = {};
	FILE* pfile = NULL;

	ioctl(devfd, PLCM_IOCTL_SET_LINE, 2);
        ioctl(devfd, PLCM_IOCTL_RETURNHOME, 0);

	pfile = popen (" df -h|grep \"/$\"|awk '{print $5}'", "r");
	if (!pfile) {
		syslog(LOG_ERR, "Can't read disk statistic\n");
		goto out;
	}
	if ((fgets (root_use, sizeof(root_use), pfile)) == NULL ) {
		syslog (LOG_ERR, "Can't read / partition use");
		pclose (pfile);
		goto out;
	}
	pclose (pfile);

	pfile = popen (" df -h|grep \"^/var\"|awk '{print $5}'", "r");
	if (!pfile) {
		syslog(LOG_ERR, "Can't read disk statistic\n");
		goto out;
	}
	/*
		It can be that no /var partiotion exist it is normal
	*/
	fgets (var_use, sizeof(var_use), pfile);
	pclose (pfile);

	if (strncmp(var_use, "", sizeof(var_use)))
		snprintf (disk_stat, sizeof(disk_stat), "/: %s, /var: %s", root_use, var_use);
	else
		snprintf (disk_stat, sizeof(disk_stat), "/: %s", root_use);

	write(devfd, disk_stat, DSPL_WIDTH);
out :
	return 0;
}

static int write_display_c () {
	char serial_number[DSPL_WIDTH] = {};
	char serial_str[DSPL_WIDTH + 1] = {};
	FILE* pfile = NULL;

	ioctl(devfd, PLCM_IOCTL_SET_LINE, 2);
        ioctl(devfd, PLCM_IOCTL_RETURNHOME, 0);

	pfile = popen ("/etc/serial", "r");
	if (!pfile) {
		syslog(LOG_ERR, "Can't read serial number");
		goto out;
	}
	if ((fgets (serial_number, sizeof(serial_number), pfile)) == NULL) {
		syslog(LOG_ERR, "Can't read serial number");
		goto close;
	}

	strtok (serial_number, "\n");
	snprintf (serial_str, sizeof(serial_str), "Serial: %s", serial_number);
	write (devfd, serial_str, DSPL_WIDTH);

close:
	pclose (pfile);

out:
	return 0; 
}

static int write_display_eth () {
	char status_message[DSPL_WIDTH + 1] = {};
	char interface_mac[DSPL_WIDTH] = {};
	struct ifreq ifreq;
        int s;
	struct ethtool_cmd edata;

	ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
        ioctl(devfd, PLCM_IOCTL_CLEARDISPLAY, 0);

        s = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (s==-1) {
                syslog (LOG_ERR, "Can't open socket");
                goto out;
        }

	strncpy(ifreq.ifr_name, network_interface_array[network_interface_index], INTERFACE_NAME_LEN);
	if(ioctl(s, SIOCGIFHWADDR, &ifreq) < 0 ) {
		syslog (LOG_ERR, "Can't read info about interface %s",
					network_interface_array[network_interface_index]);
                goto out;
	}
	snprintf (interface_mac, sizeof (interface_mac), "%02x:%02x:%02x:%02x:%02x:%02x", 
		(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[0],
		(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[1],
		(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[2],
		(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[3],
		(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[4],
		(int) ((unsigned char *) &ifreq.ifr_hwaddr.sa_data)[5] );

	if (ioctl(s, SIOCGIFFLAGS, &ifreq) < 0) {
		syslog (LOG_ERR, "Can't read interface %s state",
					network_interface_array[network_interface_index]);
                goto out;
	}

	if ((ifreq.ifr_flags) &IFF_UP) {
		if ((ifreq.ifr_flags) &IFF_RUNNING) {
			ifreq.ifr_data = (caddr_t)&edata;
			edata.cmd = ETHTOOL_GSET;

			if (ioctl(s, SIOCETHTOOL, &ifreq) < 0) {
				syslog (LOG_ERR, "Can't read info about interface speed %s",
							network_interface_array[network_interface_index]);
				goto out;
			}
			switch (edata.speed) {
       				case SPEED_10: snprintf (status_message, sizeof(status_message), "%s 10Mbps", network_interface_array[network_interface_index]); break;
				case SPEED_100: snprintf (status_message, sizeof(status_message), "%s 100Mbps", network_interface_array[network_interface_index]); break;
				case SPEED_1000: snprintf (status_message, sizeof(status_message), "%s 1Gbps", network_interface_array[network_interface_index]); break;
				case SPEED_2500: snprintf (status_message, sizeof(status_message), "%s 2,5Gbps", network_interface_array[network_interface_index]); break;
				case SPEED_10000: snprintf (status_message, sizeof(status_message), "%s 10Gbps", network_interface_array[network_interface_index]); break;
	
			}
		} else {
			snprintf (status_message, sizeof(status_message), "%s not configured",
					network_interface_array[network_interface_index]);	
		}
	} else {

		snprintf (status_message, sizeof(status_message), "%s down",
				network_interface_array[network_interface_index]);
		ioctl(devfd, PLCM_IOCTL_RESET_CGRAM, 0);
	}

	write (devfd, status_message, DSPL_WIDTH);

	ioctl(devfd, PLCM_IOCTL_SET_LINE, 2);
        ioctl(devfd, PLCM_IOCTL_RETURNHOME, 0);

	write (devfd, interface_mac, DSPL_WIDTH);

out:
	close (s);
	return 0; 
}

static int iface_is_bond_slave(int s, const char *slave, const char *master) {
      struct ifreq ifr;
      struct ifbond ifb;
      struct ifslave ifs;
      memset(&ifr, 0, sizeof(ifr));
      memset(&ifb, 0, sizeof(ifb));
      strncpy(ifr.ifr_name, master, sizeof(ifr.ifr_name));
      ifr.ifr_data = &ifb;
      if (ioctl(s, SIOCBONDINFOQUERY, &ifr) >= 0) {
            while (ifb.num_slaves--) {
                  memset(&ifr, 0, sizeof(ifr));
                  memset(&ifs, 0, sizeof(ifs));
                  strncpy(ifr.ifr_name, master, sizeof(ifr.ifr_name));
                  ifr.ifr_data = &ifs;
                  ifs.slave_id = ifb.num_slaves;
                  if ((ioctl(s, SIOCBONDSLAVEINFOQUERY, &ifr) >= 0) && 
			(strncmp(ifs.slave_name, slave, sizeof(ifs.slave_name)) == 0))
                        return 1;
            }
      }
      return 0;
}


static int write_display_eth_addr () {
	int s=0;
	int i = 0;
	struct ifconf ifc;
	struct ifreq *pIfr;
	char buf[1024];
	char interface_slave_status[DSPL_WIDTH + 1] = {};
	char interface_name[INTERFACE_NAME_LEN] = {};
	struct ifaddrs *ifap, *ifa;

	s = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (s==-1) {
                syslog (LOG_ERR, "Can't open socket");
                goto out;
        }
	strncpy (interface_name, network_interface_array[network_interface_index], INTERFACE_NAME_LEN);

	/*
		Check if interface is slave and find master's name
	*/

	if (getifaddrs(&ifap) != 0) {
        	syslog(LOG_ERR, "unable to get interface list");
		return -1;
	}

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (iface_is_bond_slave(s, network_interface_array[network_interface_index], ifa->ifa_name)) {
			ioctl(devfd, PLCM_IOCTL_SET_LINE, 1);
	        	ioctl(devfd, PLCM_IOCTL_RETURNHOME, 0);
			snprintf (interface_slave_status, sizeof(interface_slave_status), "%s(%s)", 
					network_interface_array[network_interface_index], ifa->ifa_name);
			write(devfd, interface_slave_status, DSPL_WIDTH);
			strncpy (interface_name, ifa->ifa_name, INTERFACE_NAME_LEN);
		}
	}
	freeifaddrs(ifap);

	/*
		Find interface address or master address if interface is slave
	*/

	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	ioctl(s, SIOCGIFCONF, &ifc);

	pIfr = ifc.ifc_req;
	for (i = ifc.ifc_len / sizeof(struct ifreq); --i >= 0; pIfr++) {
		if (!strncmp(interface_name, pIfr->ifr_name, INTERFACE_NAME_LEN)) {
			ioctl(devfd, PLCM_IOCTL_SET_LINE, 2);
	        	ioctl(devfd, PLCM_IOCTL_RETURNHOME, 0);
			write (devfd, inet_ntoa (((struct sockaddr_in *) &pIfr->ifr_addr)->sin_addr), DSPL_WIDTH);
			goto out;
		}
	}


out:
	close (s);
	return 0;
}

static int cmpstringp (const void *p1, const void *p2) {
	/* 
		If ethm interface exist it must be on top
	*/
	if (!strncmp(p2, "ethm", INTERFACE_NAME_LEN))
		return 1; 
	return strverscmp(p1, p2);
}

static int get_network_interface_count (void) {
	struct ifreq ifreq;
	int s, i=0;
	int interface_count = 0; 

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s==-1) {
		syslog (LOG_ERR, "Can't open socket");
		return -1;
	}

	do {
		i ++;
		ifreq.ifr_ifindex = i;
		if(ioctl(s, SIOCGIFNAME, &ifreq) < 0 )
			continue;
		if (strncmp("eth", ifreq.ifr_name, 3)) {
			// Not a eth* interface
			continue;
		}
		strncpy (network_interface_array [interface_count], ifreq.ifr_name, INTERFACE_NAME_LEN);
		interface_count++;
	} while (i < INTERFACE_MAX_COUNT);

	qsort (network_interface_array, interface_count, INTERFACE_NAME_LEN, cmpstringp);

	close(s);
	return interface_count;
}

static void check_commit_status() {
	FILE *status_file = NULL;
	COMMIT_STATUS_TYPE tmp_status = commit_status;

	commit_status_changed = 0;

	// we need to change from SUCCESS to FAIL only at boot time
	if (commit_status == COMMIT_SUCCESS)
		return;

	status_file = fopen (STATUS_FILE, "r");
	if (status_file) {
		char commit_rc[3];

		fgets (commit_rc, sizeof(commit_rc), status_file);
		if (!strncmp(commit_rc, "0\n", sizeof(commit_rc))) {
			commit_status = COMMIT_SUCCESS;
		} else {
			commit_status = COMMIT_FAIL;
		}
		fclose(status_file);
	} else {
		commit_status = COMMIT_SUCCESS;
	}

	if (commit_status != tmp_status)
		commit_status_changed = 1;
}

int read_keypad () {
	unsigned char Pre_Value = 0;
	struct sigaction act;

	devfd = open ("/dev/plcm_drv", O_RDWR);
	if(devfd == -1)	{
		syslog(LOG_ERR, "Can't open /dev/plcm_drv\n");
		return -1;
	}

	processor_count = sysconf(_SC_NPROCESSORS_ONLN);

	memset (&act, '\0', sizeof(act));
	write_display_a ();
        act.sa_handler = &write_display_a;
        sigaction(SIGALRM, &act, NULL);
        alarm (10);

	network_interface_count = get_network_interface_count ();

	/**
		TODO: process error with -1
	**/

	do {
		unsigned char Keypad_Value;

		Keypad_Value = ioctl(devfd, PLCM_IOCTL_GET_KEYPAD, 0);
		if(Pre_Value != Keypad_Value) {

			switch(Keypad_Value) {
				case KEYPAD_UP: 
					switch (current_display) {
						case DISPLAY_A:
							alarm (0);
							current_display = DISPLAY_C;
							write_display_c ();
							alarm (10);
						break;
						case DISPLAY_B:
							alarm(0);
							current_display = DISPLAY_A;
							write_display_a ();
							alarm (10);
						break;
						case DISPLAY_C:
							alarm(0);
							current_display = DISPLAY_B;
							write_display_b ();
							alarm (10);
						break;
						case DISPLAY_ETH:
							alarm(0);
							current_display = DISPLAY_ETH_ADDR;
							write_display_eth_addr ();
							alarm (10);
						break;
						case DISPLAY_ETH_ADDR:
							alarm(0);
							current_display = DISPLAY_ETH;
							write_display_eth ();
							alarm(10);
						break;
					}
				break;
				case KEYPAD_RIGHT: 
					switch (current_display) {
						case DISPLAY_A: case DISPLAY_B: case DISPLAY_C:
							alarm (0);
							network_interface_index = 0;
							current_display = DISPLAY_ETH;
							write_display_eth ();
							alarm(10);
						break;
						case DISPLAY_ETH: case DISPLAY_ETH_ADDR:
							alarm (0);
							if (network_interface_index == (network_interface_count - 1))
								network_interface_index = 0;
							else
	                	                                network_interface_index ++;
							write_display_eth ();
							alarm (10);
						break;
					}
				break;
				case KEYPAD_LEFT:
					switch (current_display) {
						case DISPLAY_A: case DISPLAY_B: case DISPLAY_C:
							alarm (0);
							network_interface_index = network_interface_count - 1;
							current_display = DISPLAY_ETH;
							write_display_eth ();
							alarm(10);
						break;
				
						case DISPLAY_ETH: case DISPLAY_ETH_ADDR:
							alarm (0);
							if (network_interface_index == 0)
								network_interface_index = network_interface_count - 1;
							else 
								network_interface_index --;
							write_display_eth();
							alarm (10);
						break;
					}
				break;
				case KEYPAD_DOWN:
					switch (current_display) {
						case DISPLAY_A:
							alarm(0);
							current_display = DISPLAY_B;
							write_display_b ();
							alarm (10);
						break;
						case DISPLAY_B:
							alarm(0);
							current_display = DISPLAY_C;
							write_display_c ();
							alarm (10);
						break;
						case DISPLAY_C:
							alarm(0);
							current_display = DISPLAY_A;
							write_display_a ();
							alarm (10);
						break;
						case DISPLAY_ETH:
							alarm(0);
							current_display = DISPLAY_ETH_ADDR;
							write_display_eth_addr ();
							alarm (10);
						break;
						case DISPLAY_ETH_ADDR:
							alarm(0);
							current_display = DISPLAY_ETH;
							write_display_eth ();
							alarm(10);
						break;
					}
				break;
			}
			Pre_Value = Keypad_Value;
		}
		usleep(100000); // 100 msec
	} while (1);

	close(devfd);
	return 0;
}

int main(int argc, char *argv[]) {
	pid_t pid, sid;

	syslog(LOG_NOTICE, "%s daemon starting up", DAEMON_NAME);

	// Setup syslog logging - see SETLOGMASK(3)
	setlogmask(LOG_UPTO(LOG_INFO));
	openlog(DAEMON_NAME, LOG_CONS, LOG_USER);

	pid = fork ();

	if (pid < 0) {
		syslog (LOG_ERR, "fork failed\n");
		exit (EXIT_FAILURE);
	}

	if (pid > 0)
		exit (EXIT_SUCCESS);

	umask (0);

	sid = setsid ();
	if (sid < 0)
		exit (EXIT_FAILURE);

	if ((chdir("/")) < 0) {
                exit(EXIT_FAILURE);
        }

	close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
	
	return read_keypad();
}

