/*
 * queue.c
 *
 *  Created on: Jul 3, 2012
 *      Author: vikram
 */

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#define FAILURE -999
#define SUCCESS 0

struct queue_ds {
	int piles[20];
	int head;
	int tail;
};

typedef struct queue_ds queue;
//Instead of efficiency focus on correctness
int is_queue_full(queue **q_ref) {	//if you are updating pointer it is advisable use double pointer
									//However,if you just updating values like index which are not
									//type of pointer use single pointer
	if((*q_ref)->head == 20) {
		if ((*q_ref)->head == (*q_ref)->tail) {
			//update head =>0 and tail =>0
			(*q_ref)->head = 0;
			(*q_ref)->tail = 0;
		}
		printf("queue is full \n");
		return FAILURE;
	}
	return SUCCESS;
}

int is_queue_empty(queue *q_ptr) {
	if(q_ptr->tail == q_ptr->head) {
		printf("queue is empty \n");
		return FAILURE; //stack is empty
	}
	return SUCCESS;
}


void enqueue(queue **q_ref,int payload){
	//Insert element
	if(is_queue_full(q_ref)== SUCCESS) {
		(*q_ref)->piles[(*q_ref)->head] = payload;
		(*q_ref)->head += 1;
		printf("enqueued element = %d\n",payload);
	}
}

int dequeue(queue** q_ref) {
	//remove element
	if(is_queue_empty(*q_ref) == SUCCESS) {
		int payload = (*q_ref)->piles[(*q_ref)->tail];
		(*q_ref)->tail += 1;
		printf("dequeued element = %d\n",payload);
		return (payload);
	}
}

int main(int argc,char** argv){
	queue *q_ptr = (queue *) malloc(sizeof(queue));
	memset(q_ptr,0,sizeof(queue));
	int i = 0;
	for(i=0;i<10;i++) {
		enqueue(&q_ptr,i);
	}
	dequeue(&q_ptr);

	return(0);
}
