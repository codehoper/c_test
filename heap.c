/*
 * heap.c
 *
 *  Created on: Aug 6, 2012
 *      Author: vikram
 */

#include<stdio.h>
#include<stdlib.h>

#define MAX_LIMIT 20

typedef struct {
	int index;
	int pri_q[MAX_LIMIT];
}  priority_q;

static int get_parent(int n) {
	return (n/2);
}

static int get_child(int n) {
	return (2 * n);
}

static void do_swap(int *a,int *b) {
	*a = *a + *b;
	*b = *a - *b;
	*a = *a - *b;
}

/**
 * For bubble up compare with parent
 * recursively until first element.
 */
static void bubble_up(priority_q *q,int n) {		//for recursive call have to give 2 para

	int parent = get_parent(n);
	if(parent == 0) {
		return;
	}else {
		if(q->pri_q[parent] < q->pri_q[n]) {
			do_swap(&(q->pri_q[parent]),&(q->pri_q[n]));
		}
		bubble_up(q,parent);
	}
}

static void insert_q(priority_q *q,int data) {

	if(q->index == MAX_LIMIT) {
		printf("q is full \n");
	}else {
		q->index += 1;
		q->pri_q[q->index] = data;
		//maintain heap property
		bubble_up(q,q->index);
	}

}
/***
 * For bubble down we use children
 * recursively until last element
 */
static void bubble_down(priority_q *q,int n) {
	int i;
	int max_index = n;
	int child = get_child(n);
	if(max_index >= q->index) { //If it is only element left in the heap
		return;
	}else {
		//Use left and right children
		for(i =0;i<=1;i++) {
			if(q->pri_q[child + i] > q->pri_q[max_index]) {
				max_index = child+i; //update max index
			}
		}
		//If child is greater than root/parent
		if(max_index != n) {
			do_swap(&(q->pri_q[max_index]),&(q->pri_q[n]));
			bubble_down(q,max_index);
		}
	}
}

/**
 * Always remove the top element i.e. first element of array
 */
static int remove_max(priority_q *q) {

	if(q->index <= 0) {
		printf("q is empty\n");
	}

	int max_no = q->pri_q[1];
	//Replace the first element with last element
	q->pri_q[1] = q->pri_q[q->index];
	q->pri_q[q->index] = 0;
	q->index -= 1; //update index

	bubble_down(q,1);
	return max_no;
}

int main(int argc,char **argv) {
	priority_q *q = (priority_q *)malloc(sizeof(priority_q));
	q->index = 0;
	int i = 0;

	for(i=1;i<=10;i++) {
		printf("%d \t",i);
		insert_q(q,i);
	}
	printf("\n");
	for(i=1;i<=10;i++) {
		printf("%d \t",q->pri_q[i]);
	}

	printf("\n");
	for(i=1;i<=10;i++) {
		printf("%d \t",remove_max(q));
	}


	//Heapsort is just create heap and extract max or min

}
