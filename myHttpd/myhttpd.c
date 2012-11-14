/*
 * myhttpd.c
 *
 *  Created on: Oct 7, 2011
 *      Author: vikram
 */

#include "myhttpd.h"


/****************************************************************************
 * Initialize priority queue
 ****************************************************************************/
void prioq_init(priority_queue* h) {
	h->size=0;
}

/***************************************************************************
 *Adjust the position according to the priority
 *************************************************************************/
void prioq_prioterize(priority_queue* h,int i) {
	int l,r,smallest;
	running_queue_sjf* tmp;
	l=2*i; /*left child*/
	r=2*i+1; /*right child*/

	if ((l < h->size)&&(h->running_queue_sjfs[l]->filesize_t < h->running_queue_sjfs[i]->filesize_t))
		smallest=l;
	else
		smallest=i;
	if ((r < h->size)&&(h->running_queue_sjfs[r]->filesize_t < h->running_queue_sjfs[smallest]->filesize_t))
		smallest=r;
	if (smallest!=i) {
		/*exchange to maintain heap property*/
		tmp=h->running_queue_sjfs[smallest];
		h->running_queue_sjfs[smallest]=h->running_queue_sjfs[i];
		h->running_queue_sjfs[i]=tmp;
		prioq_prioterize(h,smallest);
	}
}
/*************************************************************************
 * Adds the element in the priority queue
 ***********************************************************************/
void prioq_addItem(priority_queue* h,running_queue_sjf* running_queue_sjf) {
	unsigned int i,parent;
	h->size=h->size+1;
	i=h->size-1;
	parent=i/2;
	/*find the correct place to insert*/
	while ((i > 0)&&(h->running_queue_sjfs[parent]->filesize_t > running_queue_sjf->filesize_t)) {
		h->running_queue_sjfs[i]=h->running_queue_sjfs[parent];
		i=parent;
		parent=i/2;
	}
	h->running_queue_sjfs[i]=running_queue_sjf;
	printf("\nreq added to priority queue");
}
/**************************************************************************
 * Remove the element from the priority queue
 ************************************************************************/
running_queue_sjf* prioq_extractMin(priority_queue* h) {
	running_queue_sjf* max;
	if (prioq_isEmpty(h))
		return 0;
	max=h->running_queue_sjfs[0];
	h->running_queue_sjfs[0]=h->running_queue_sjfs[h->size-1];
	h->size=h->size-1;
	prioq_prioterize(h,0);
	printf("request removed from the queue");
	return max;
}

/**********************************************************************
 * Verify queue is empty or not
 *********************************************************************/
int prioq_isEmpty(priority_queue *h) {
	return h->size==0;
}

/*************************************************************************
 * Verify priority queue is full or not
 *************************************************************************/
int prioq_isFull(priority_queue *h) {
	return h->size>=MAX_SIZE;
}

/*************************************************************************
 * Add the request to running queue
 *
 ************************************************************************/
void add_request_runningq(running_queue *element) {

	current_rq = (running_queue*)malloc(sizeof(running_queue));
	strncpy(current_rq->request,element->request,REQUEST_SIZE);
	strcpy(current_rq->modifiedTime,element->modifiedTime);
	strcpy(current_rq->type,element->type);
	strcpy(current_rq->logmsg,element->logmsg);
	current_rq->conn_id = element->conn_id;
	current_rq->file_size = element->file_size;
	current_rq->next = NULL;

	if(is_first_element_rq == 1) {
		is_first_element_rq  = 0;
		front_rq = current_rq;
		rear_rq = current_rq;
	}
	else {
		rear_rq->next = current_rq;
		rear_rq = current_rq;
	}
	printf("Request is added to running queue\n");
}

/**************************************************************************
 * Remove the request from the queue
 ************************************************************************/
running_queue *remove_request_runningq() {

	running_queue * ret;
	if(front_rq == NULL) {
		puts("\n Running Queue is empty");
	}
	else
	{
		if(front_rq == rear_rq) {
			is_first_element_rq = 1;
		}
		current_rq = front_rq;
		front_rq = front_rq->next;
		current_rq->next = NULL;
		ret = current_rq;
	}
	printf("Request is removed from running queue\n");
	return ret;
}

/**************************************************************************
 * Add request to ready queue
 ************************************************************************/
void add_request(ready_queue *element) {

	current = (ready_queue*)malloc(sizeof(ready_queue));
	strncpy(current->request,element->request,REQUEST_SIZE);
	strcpy(current->logmsg,element->logmsg);
	current->conn_id = element->conn_id;
	current->next = NULL;

	if(is_first_element == 1) {
		is_first_element  = 0;
		front = current;
		rear = current;
	}
	else {
		rear->next = current;
		rear = current;
	}
	printf("Request is added to ready queue\n");
}
/*************************************************************************
 * Remove the request from the ready queue
 *
 ***********************************************************************/
ready_queue *remove_request() {

	ready_queue * ret;
	if(front == NULL) {
		puts("\n Queue is empty");
	}
	else
	{
		if(front == rear) {
			is_first_element = 1;
		}
		current = front;
		front = front->next;
		current->next = NULL;
		ret = current;
	}
	printf("Request is removed from ready queue\n");
	return ret;
}
/****************************************************************************
 *Get the GMT time
 ***************************************************************************/

char* getGMTtime() {
	char *gmtTime;
	char str[30] ;
	char str1[10] ;
	time_t secs;
	struct tm * timeinfo;
	time ( &secs );
	timeinfo = localtime(&secs);
	strftime(str, 30, "%d/%b/%Y:%H:%M:%S",timeinfo);
	sprintf(str1," -%s","0600");
	gmtTime =  strcat(str,str1);
	return gmtTime;

}
/**************************************************************************
 * Get the user name
 *************************************************************************/
char *getUserName () {
	register struct passwd *pw;
	register uid_t uid;
	char *username;
	uid = geteuid();
	pw = getpwuid (uid);
	if (pw)
	{
		username = (char *)malloc(strlen(pw->pw_name)+1*sizeof(char));
		strcpy(username,pw->pw_name);
		return username;
	}
	else{
		perror("Unable to find the username");
		return NULL;
	}
}

/*************************************************************************
 * Print the usage info to stdout and exit the application
 ***********************************************************************/
void show_usage_info() {
	printf("\n -d :Enter in debug mode "
			"\n -h :Print usage summary "
			"\n -l file :log all request to given file"
			"\n -p port :listen to the given port"
			"\n -r dir :set the root directory for http server"
			"\n -t time :set the queuing time in second"
			"\n -n threadnum no of threads in thread pool"
			"\n -s sched : set the scheduling policy FCFS/SJF");
	exit(1);
}

/*************************************************************************
 *  display error and exit application
 *
 *************************************************************************/
void show_error(char *err)
{
	perror(err);
	exit(1);
}

/*************************************************************************
 *  create socket at server and listen specified port number
 *
 *************************************************************************/
int create_socket (int *port_number) {
	int socket_fd;
	struct sockaddr_in server_addr;

	//create socket
	socket_fd = socket (AF_INET, SOCK_STREAM, 0);
	if(socket_fd == -1) {
		show_error("socket creation failed");
	}
	//Initialize the server address with 0
	bzero(&server_addr, sizeof(server_addr));

	//Set the server address
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl (INADDR_ANY);
	server_addr.sin_port = htons (port_no);

	if ((bind(socket_fd, (struct sockaddr *) &server_addr,
			sizeof(server_addr)))<0){
		show_error("failed to bind");
	}

	//convert to passive socket
	if((listen(socket_fd,BACK_LOG_QUEUE))<0) {
		show_error("Failed to listen");
	}

	return (socket_fd);
}

ready_queue * remove_request_fromq() {
	ready_queue *service;
	service = remove_request();
	return (service);
}

/*************************************************************************
 * Arrange the running queue
 ************************************************************************/
void arrange_runningq(void *arg) {
	running_queue *req = (running_queue *) arg;
	add_request_runningq(req);
}


/*************************************************************************
 * choose request from queue and perform scheduling it to one of the
 * execution thread
 *
 *************************************************************************/
void  schedule_thread() {
	int is_first_visit = 1;
	int is_first_init = 0;
	while(1) {
		//sleep for t+1 time
		ready_queue *request;
		running_queue *resp;
		if(is_first_visit) {
			sleep(queue_time);
		}
		sem_wait (&req_queue_count); //check the value of count
		//remove the request from the queue
		pthread_mutex_lock(&ready_mut);
		request = remove_request_fromq();
		resp = (running_queue *)calculate_reponse_para(request);
		if(is_sjf == 0) {
			arrange_runningq(resp);
		}
		else {
			if(is_first_init == 0) {
				prioq_init(&heap);
			}
			//get the mutex lock
			pthread_mutex_lock(&sjf_mut);
			prio_q[index_t].conn_id_t = resp->conn_id;
			prio_q[index_t].filesize_t = resp->file_size;
			strcpy(prio_q[index_t].logmsg_t,resp->logmsg);
			strcpy(prio_q[index_t].modifiedTime_t,resp->modifiedTime);
			strcpy(prio_q[index_t].request_t,resp->request);
			strcpy(prio_q[index_t].type_t,resp->type);
			prioq_addItem(&heap,&(prio_q[index_t]));
			index_t ++;
			is_first_init ++;
			pthread_mutex_unlock(&sjf_mut);
			//release the lock
		}
		sem_post(&sched_queue_count);
		pthread_mutex_unlock(&ready_mut);
		is_first_visit = 0;
	}
}

/*************************************************************************
 * Calculate the response parameter
 ************************************************************************/
void* calculate_reponse_para(void *arg) {

	ready_queue *service = (ready_queue *) arg;
	running_queue *resp = (running_queue *)malloc(sizeof(running_queue));
	DIR  *d;
	struct dirent *sdir;
	struct stat st;
	char *method,*fstr,*tempstr;
	char buffer [REQUEST_SIZE+1];
	int i,len,buflen;
	int status = 0;
	resp->conn_id = service->conn_id;
	strcpy(resp->request,service->request);
	strcpy(resp->logmsg,service->logmsg);

	method = strtok(service->request," ");
	tempstr = strtok(NULL,"");
	strcpy(buffer,tempstr);
	if(strcasecmp(method,"GET")==0) {

		for(i=0;i<BUFSIZE;i++) {
			if(buffer[i] == ' ' || buffer[i] == '\r' || buffer[i] == '\n') {
				buffer[i] = 0;
				break;
			}
		}
		buflen=strlen(buffer);
		fstr = (char *)0;
		for(i=0;extensions[i].ext != 0;i++) {
			len = strlen(extensions[i].ext);
			if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
				fstr =extensions[i].filetype;
				strncpy(resp->type,extensions[i].ext,len);
				break;
			}
		}
		if(fstr == 0) {
			//User inputs invalid file format
			resp->file_size = -3;
		}
		else {
			chdir(rootDir);
			d = opendir(rootDir);
			if (d)
			{
				while ((sdir = readdir(d)) != NULL)
				{
					if(strcasecmp("index.html",&buffer[1])==0){
						status = 1;
						resp->file_size = 20;
						strcpy(resp->modifiedTime,"0.00");
						break;
					}
					else if(strcasecmp(sdir->d_name,&buffer[1])==0)
					{
						status = 1;
						stat(sdir->d_name, &st);
						resp->file_size = st.st_size;
						strcpy(resp->modifiedTime,ctime(&st.st_mtim));
						break;
					}
				}

				closedir(d);
			}
		}

	}
	else {
		if(!strcasecmp(method,"HEAD")) {
			resp->file_size = 0;
			status = 1;
			strcpy(resp->modifiedTime,"0.00");
		}
		//invalid request like POST/other
		else {
			status = 1;
			resp->file_size = -2;
		}
	}

	//file not found
	if(status == 0) {
		resp ->file_size = -1;
	}

	return (resp);
}

/************************************************************************
 *Populate the log content
 *************************************************************************/
void *populate_log_content(void *arg) {

	log_content *logs = (log_content *)malloc(sizeof(log_content));

	if(is_sjf == 1) {
		running_queue_sjf * req = (running_queue_sjf *)arg;
		strcpy(logs->readyq_log_content,req->logmsg_t);
		strcpy(logs->request_header,req->request_t);
		logs->content_length = req->filesize_t;
		if(logs->content_length == -1) {
			logs->request_status = 404;
		}
		else if(logs->content_length == -2) {
			logs->request_status = 300;
		}
		else if(logs->content_length == -3) {
			logs->request_status = 415;
		}
		else {
			logs->request_status = 200;
		}
	}
	else {
		running_queue * req = (running_queue *)arg;
		strcpy(logs->readyq_log_content,req->logmsg);
		strcpy(logs->request_header,req->request);
		logs->content_length = req->file_size;
		if(logs->content_length == -1) {
			logs->request_status = 404;
		}
		else if(logs->content_length == -2) {
			logs->request_status = 300;
		}
		else if(logs->content_length == -3) {
			logs->request_status = 415;
		}
		else {
			logs->request_status = 200;
		}
	}
	strcpy(logs->schedule_time,getGMTtime());
	return (logs);
}

/*************************************************************************
 * populate the http response
 ***********************************************************************/
void *populate_http_response_all(void *arg) {

	time_t cur_time;
	struct tm *local_time;
	cur_time = time (NULL);
	local_time = localtime (&cur_time);
	char *time = asctime (local_time);
	char serverBuffer[80] = {0,};
	gethostname(serverBuffer,sizeof(serverBuffer));
	response_header *http_reponse = (response_header*)calloc(1,sizeof(response_header));
	strcpy(http_reponse->current_time,time);
	strcpy(http_reponse->server,serverBuffer);

	if (is_sjf == 0) {
		running_queue *service;
		service = (running_queue *)arg;
		strcpy(http_reponse->content_type,service->type);
		strcpy(http_reponse->last_modified_date,service->modifiedTime);
		strcpy(http_reponse->filename,service->request);
		http_reponse->content_length = service->file_size;

	}
	else {
		running_queue_sjf *service_t;
		service_t = (running_queue_sjf *)arg;
		strcpy(http_reponse->content_type,service_t->type_t);
		strcpy(http_reponse->last_modified_date,service_t->modifiedTime_t);
		strcpy(http_reponse->filename,service_t->request_t);
		http_reponse->content_length = service_t->filesize_t;
	}

	return (http_reponse);
}

/*************************************************************************
 *  send the HTTP response
 *
 *************************************************************************/
void send_http_response() {

	while(1) {
		sem_wait (&sched_queue_count);
		pthread_mutex_lock (&send_mut);
		char log_msg[1024] = {'\0'};
		response_header *resp_header;
		log_content *logs;
		int conn_id_t;
		if(is_sjf == 0) {
			running_queue *req = remove_request_runningq();
			resp_header = (response_header *)populate_http_response_all(req);
			conn_id_t = req->conn_id;
			logs = populate_log_content(req);
		}
		else {
			if (!prioq_isEmpty(&heap)) {
				if(index_t <= 1) {
					sleep(20);
				}
				pthread_mutex_lock(&sjf_mut);
				q_ele = prioq_extractMin(&heap);
				resp_header = (response_header *)populate_http_response_all(q_ele);
				conn_id_t = q_ele->conn_id_t;
				logs = populate_log_content(q_ele);
				index_t --;
				pthread_mutex_unlock(&sjf_mut);
			}

		}


		char respbuffer[255] = {'\0'};
		if(resp_header->content_length == -1) {
			sprintf(respbuffer, "HTTP/1.0 404 Not Found\r\n");
			send(conn_id_t, respbuffer, strlen(respbuffer), 0);
			sprintf(respbuffer, "Server: %s\r\n",resp_header->server);
			send(conn_id_t, respbuffer, strlen(respbuffer), 0);

		}
		else if (resp_header->content_length == -2) {
			sprintf(respbuffer, "HTTP/1.0 400 Bad Request\r\n");
			send(conn_id_t, respbuffer, strlen(respbuffer), 0);
			sprintf(respbuffer, "Server: %s\r\n",resp_header->server);
			send(conn_id_t, respbuffer, strlen(respbuffer), 0);

		}
		else {
			sprintf(respbuffer, "HTTP/1.0 200 Ok\r\n");
			send(conn_id_t, respbuffer, strlen(respbuffer), 0);
			sprintf(respbuffer, "Server: %s\r\n",resp_header->server);
			send(conn_id_t, respbuffer, strlen(respbuffer), 0);
			sprintf(respbuffer, "Last Modified %s\r",resp_header->last_modified_date);
			send(conn_id_t, respbuffer, strlen(respbuffer), 0);
			sprintf(respbuffer, "Content type %s\r\n",resp_header->content_type);
			send(conn_id_t, respbuffer, strlen(respbuffer), 0);
			sprintf(respbuffer, "Content length %d\r\n",resp_header->content_length);
			send(conn_id_t, respbuffer, strlen(respbuffer), 0);
		}

		//send the content
		if(resp_header->content_length > 0 ) {
			//Read the content of the file
			char buffer[256];
			int i;
			int ret;
			int file_fd;
			int dir_id;
			DIR *dir_open;
			struct dirent *sdir;
			strcpy(buffer,resp_header->filename);
			for(i=4;i<256;i++) {
				if(buffer[i] == ' '|| buffer[i]=='\r' || buffer[i]=='\n') {
					buffer[i] = 0;
					break;
				}
			}
			dir_id = chdir(rootDir);
			if(strcasecmp("index.html",&buffer[5])==0) {
				dir_open = opendir(rootDir);
				if (dir_open)
				{
					(void)write(conn_id_t,"Directory Listing\n",22);
					while ((sdir = readdir(dir_open)) != NULL)
					{
						if(!(strcmp(sdir->d_name,".")==0 || strcmp(sdir->d_name,"..")==0))
						{
							sprintf(buffer, "%s\r\n",sdir->d_name);
							(void)send(conn_id_t,buffer,strlen(sdir->d_name)+2,0);
						}
					}
				}
				closedir(dir_open);
			}
			else if((file_fd = open(&buffer[5],O_RDONLY)) != -1){
				while ((ret = read(file_fd, buffer,1024)) > 0 ) {
					(void)send(conn_id_t,buffer,ret,0);
				}
				close(file_fd);
			}
		}
		//close the connection
		close(conn_id_t);

		//print the log
		sprintf(log_msg,"%s  [%s]\t %s %d %d\n",
				logs->readyq_log_content,logs->schedule_time,
				logs->request_header,
				logs->content_length,logs->request_status);

		if(debug_info == 0) {
			int fd;

			fd = open(log_fileName,O_CREAT|O_APPEND|O_WRONLY,0777);
			if(fd == -1) {
				perror("Error while writing in file ");
				exit(1);
			}

			(void)write(fd,log_msg,strlen(log_msg));
			(void)write(fd,"\n",1);
			(void)close(fd);
		}
		else {
			printf("%s\n",log_msg);
		}

		pthread_mutex_unlock(&send_mut);
	}
}



/*******************************************************************
 * Main thread acts as listener thread
 * Listens http request from client
 *******************************************************************/

int main (int argc, char **argv) {

	int socket_fd,conn_client_fd;
	int i = 0;
	int request_count =0;
	struct sockaddr_in client_addr;
	socklen_t sock_length;
	pthread_t sid;
	pthread_t workid[thread_num];
	char buffer[REQUEST_SIZE+1];
	char ipstr[INET_ADDRSTRLEN ]; //For remote machine address
	char local_dir_buffer[40];

	//Initialize the mutexes and semaphore
	pthread_mutex_init(&send_mut, NULL);
	pthread_mutex_init(&ready_mut, NULL);
	pthread_mutex_init(&sjf_mut,NULL);
	sem_init (&req_queue_count, 0, 0);
	sem_init(&sched_queue_count,0,0);

	//Parse the argument
	parse_ip_argument(argc,argv);


	//Show help
	if(print_usage == 1) {
		show_usage_info();
	}

	//Get scheduling scheme
	else if(!strcasecmp(sched,"SJF")) {
		is_sjf = 1;
	}

	if(debug_info == 1) {
		//Enter in debugging mode and print out the log to stdout
	}
	else {
		//create the daemon process
		daemon(0,0);
	}

	//server directory
	if(!strcmp(rootDir,"")) {
		char u_name[20];
		strcpy(u_name,getUserName());
		sprintf(local_dir_buffer,"/home/%s/myhttpd",u_name);
		int cnt = strlen(local_dir_buffer);
		rootDir = (char *)malloc(cnt + 1*sizeof(char));
		strcpy(rootDir,local_dir_buffer);
	}

	//Create the socket
	socket_fd = create_socket(&port_no);

	//Create schedular thread
	//(void*(*)(void*)) ======> this pointer
	pthread_create(&sid,NULL,(void*(*)(void*))schedule_thread,NULL);
//	pthread_create(&sid,NULL,(void*) & schedule_thread,NULL);

	//Create pool of worker thread
	for (i=0;i<thread_num;i++) {
		pthread_create(&workid[i],NULL,(void*(*)(void*))send_http_response,NULL);
	}

	//Listen/Main thread listen continuously
	while(1)  {
		sock_length = sizeof(client_addr);

		//accept the client connection
		conn_client_fd = accept(socket_fd,
				(struct sockaddr *) &client_addr, &sock_length);

		//Get the address of the remote machine
		getpeername(socket_fd, (struct sockaddr*)&client_addr, &sock_length);
		struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
		inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);

		if(conn_client_fd > 0) {
			char temp_time[30];
			char local_log_buf[80];
			int ret = 0;
			ret = read(conn_client_fd,buffer,REQUEST_SIZE+1);

			if(ret == 0 || ret == -1) {
				show_error("read failure");
			}
			ready_queue *request_element;
			request_element = (ready_queue *)calloc(1,sizeof(ready_queue));
			strcpy(request_element->request,buffer);

			strcpy(temp_time,getGMTtime());
			sprintf(local_log_buf,"%s - [%s]",ipstr,temp_time);
			strcpy(request_element->logmsg,local_log_buf);

			request_element->conn_id = conn_client_fd;
			pthread_mutex_lock(&ready_mut);
			add_request(request_element);
			pthread_mutex_unlock(&ready_mut);
			request_count++;
			sem_post(&req_queue_count);
		}

		else {
			show_error("Error while connecting to client");
			pthread_exit(NULL);
		}
	}

	return (0);
}


