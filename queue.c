#include<stdio.h>
#include<stdlib.h>

/****************
 * Queue operation
 */

#define MAX 100
#define TRUE 0
#define FALSE 1

typedef struct queue {
	int data[MAX + 1];		//will start the queue from 1 instead of 0
	int head;				//to insert
	int tail;				//to delete
	int count;				//to count the number of element
							//to check is_empty and is_full condition
} queue;

void insert_data(queue **q,int data);
int remove_data(queue **q);
int is_empty(queue *q);
int is_full(queue *q);

int is_empty(queue *q) {

	if(q->count <= 0) {
		return TRUE;
	} return FALSE;
}

int is_full(queue *q) {

	if(q->count >= MAX) {
		return TRUE;
	} return FALSE;

}

void initq(queue **q) {
	*q = (queue *)malloc(sizeof(queue));
	(*q)->count = 0;
	(*q)->head = 1;
	(*q)->tail = 1;
}
void insert_data(queue **q,int data) {

	if(is_full(*q) == FALSE) {
		(*q)->count += 1;
		(*q)->data[(*q)->tail] = data;
		(*q)->tail = ((*q)->tail + 1) % MAX;
	}

}

int remove_data(queue **q) { ///int remove_data(queue **q) => if you want to modify the pointer in
								//structure "queue" but rt now there is no pointer so don't use **
								//use *

	int data;
	if(is_empty(*q) == FALSE) {
		(*q)->count -= 1;
		data = (*q)->data[(*q)->head];
		(*q)->head = ((*q)->head + 1) % MAX;
	}
	return (data);
}

void traverse_q(queue *q) {
	int start = q->head;
	while(start < q->tail) {
		printf("%d \t",q->data[start]);
		start = (start + 1) % MAX;
	}
}

int main(int argc,char **argv) {

	queue *testq = NULL;
	initq(&testq);
	int arr[] = {12,34,5,6,7,8};
	int i;
	for(i=0;i<6;i++) {
		insert_data(&testq,arr[i]);
	}

	printf(" \n");
	traverse_q(testq);
	int j;
	j = remove_data(&testq);
//	printf("%d \t",j);

}


