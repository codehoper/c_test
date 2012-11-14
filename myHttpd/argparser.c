/*
 * argparser.c
 *
 *  Created on: Oct 15, 2011
 *      Author: vikram
 */


#include "boot.h"
#include "argparser.h"

int debug_info = DEF_DEBUG_INFO;
int log_info = DEF_LOG_INFO;
int print_usage = DEF_PRINT_USAGE;
int thread_num = DEF_THREAD_NUM;
int port_no = SERVER_PORT;
int queue_time = DEF_QUEUE_TIME;
char *log_fileName = DEF_LOG_FILE_NAME;
char *sched = DEF_SCHEDULE_SCHEME;
char *rootDir = DEF_ROOT_DIR;
extern char *optarg;



/**
 * Parse the input argument
 */
void parse_ip_argument(int argc, char **argv) {

	int ch;

	while((ch = getopt(argc,argv,"dhl:p:r:t:n:s:")) != -1) {

		switch (ch) {
		case 'd':
			debug_info = 1;
			break;
		case 'h':
			print_usage = 1;
			break;
		case 'l':
			if (optarg!= NULL){
				log_fileName = optarg;
				log_info = 1;
			}
			break;
		case 'p':
			if (optarg!= NULL)
				port_no = atof(optarg);
			break;
		case 'r':
			if (optarg!= NULL)
				rootDir = optarg;
			break;
		case 't':
			if (optarg!= NULL)
				queue_time = atoi(optarg);
			break;
		case 'n':
			if (optarg!= NULL)
				thread_num = atoi(optarg);
			break;
		case 's':
			if (optarg!= NULL)
				sched = optarg;
			break;
		case '?':
			printf("Please enter input correctly");
			break;
		default:
			break;
		}
	}
}

