/*
 * boot.h
 *
 *  Created on: Oct 9, 2011
 *      Author: vikram
 */

#ifndef BOOT_H_
#define BOOT_H_
#include <sys/types.h>
#include <sys/socket.h>	/**/
#include <netinet/in.h>	/**/
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>	/**/
#include <errno.h>	/**/
#include <string.h>	/**/
#include <fcntl.h>    /**/
#include <pthread.h>  /* thread library*/
#include <getopt.h>  /* for parse input argument*/
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include <pwd.h>
#include <dirent.h>


#define SERVER_PORT 8080
#define BACK_LOG_QUEUE 5
#define MAXLINE 20
#define BUFSIZE 255
#define REQUEST_SIZE 255
#define	DEF_DEBUG_INFO 0
#define	DEF_LOG_INFO  0
#define	DEF_PRINT_USAGE 0
#define	DEF_QUEUE_TIME 60
#define	DEF_THREAD_NUM 2
#define	DEF_SCHEDULE_SCHEME "FCFS"
#define	DEF_LOG_FILE_NAME "log.txt"
#define DEF_ROOT_DIR ""
#define MAX_SIZE 30


#endif /* BOOT_H_ */
