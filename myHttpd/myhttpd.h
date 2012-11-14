/*
 * myhttpd.h
 *
 *  Created on: Oct 24, 2011
 *      Author: vikram
 */

#ifndef MYHTTPD_H_
#define MYHTTPD_H_

#include "boot.h"
#include "argparser.h"

extern int debug_info;
extern int log_info;
extern int print_usage;
extern int thread_num;
extern int port_no;
extern int queue_time;
extern char *log_fileName;
extern char *rootDir;
extern char *sched;
extern void parse_ip_argument(int argc, char **argv);
int is_sjf = 0;


//Ready queue
struct job {
	char request[REQUEST_SIZE+1];
	int conn_id;
	char logmsg[255];
	struct job *next;
};
typedef struct job ready_queue;

//Running queue
struct running_job {
	char request[REQUEST_SIZE+1];
	int conn_id;
	int file_size;
	char type[20];
	char modifiedTime[40];
	char logmsg[255];
	struct running_job *next;
};
typedef struct running_job running_queue;

struct running_job_sjf
{

	char request_t[REQUEST_SIZE+1];
	int conn_id_t;
	int filesize_t;
	char type_t[20];  //type - file type
	char modifiedTime_t[40];
	char logmsg_t[255];
};

typedef struct running_job_sjf running_queue_sjf;

//Priority queue is used for the shortest Job first as worst case O(logN)
//for find minimum which is best among sorting and linear search
struct priority{
	running_queue_sjf* running_queue_sjfs[MAX_SIZE];
	int size;
};
typedef struct priority priority_queue;

struct {
	char *ext;
	char *filetype;
} extensions [] = {
		{"gif", "image/gif" },
		{"txt", "text/txt" },
		{"htm", "text/html" },
		{"html","text/html" },
		{0,0} };

//Response header
struct header{
	char current_time[40];
	char server[50];
	char last_modified_date[40];
	char content_type[20];
	int  content_length;
	char filename[256];
};
typedef struct header response_header;

struct log_struct {

	char remote_address[10];
	char readyq_log_content[512];
	char schedule_time[30]; //time when request removed from running/priority queue
	char request_header[256];
	int request_status;
	int content_length;

};
typedef struct log_struct log_content;

void show_error(char *err);
void show_usage_info();
int create_socket (int *port_number);
void schedule_thread();
void add_request(ready_queue *);
ready_queue *remove_request();
void add_request_runningq(running_queue *element);
void arrange_runningq(void *arg);
running_queue *remove_request_runningq();
void prioq_init(priority_queue* h);
void prioq_addItem(priority_queue* h,running_queue_sjf* running_queue_sjf);
running_queue_sjf* prioq_extractMin(priority_queue* h);
int prioq_isEmpty(priority_queue *h);
int prioq_isFull(priority_queue *h);
void* calculate_reponse_para(void *arg);
void add_request_runningq(running_queue *);
char* getGMTtime();
void *populate_http_response_all(void *arg);
void send_http_response();


ready_queue *current,*front,*rear;
running_queue *current_rq,*front_rq,*rear_rq;
priority_queue heap;
running_queue_sjf prio_q[MAX_SIZE];
running_queue_sjf* q_ele;
static int is_first_element = 1;
static int is_first_element_rq = 1;
pthread_mutex_t send_mut;
pthread_mutex_t ready_mut;
pthread_mutex_t sjf_mut;
sem_t req_queue_count;
sem_t sched_queue_count;
static int index_t = 0;





#endif /* MYHTTPD_H_ */
