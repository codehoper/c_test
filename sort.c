/*
 * sort.c
 *
 *  Created on: Aug 4, 2012
 *      Author: vikram
 */

#include<stdio.h>

static void do_swap(int *a,int *b) {
	int temp = *a + *b;
	*b = temp - *b;
	*a = temp - *b;
	//	*a = *a + *b;
	//	*b = *a - *b;
	//	*a = *a - *b;
}

static void do_swap1(int *a,int *b) {
	*a = *a + *b;
	*b = *a - *b;
	*a = *a - *b;
}


static void selection_sort(int u_arr[],int len) {
	//select the minimum and put it in first place and so on
	int i = 1;
	int j;
	for(i = 0; i< len ; i++) {
		int min_value = u_arr[i];			//select the minimum value
		for(j = i+1;j< len;j++) {
			if(u_arr[j] < min_value) {		//compare all the min values
				min_value = u_arr[j];
				do_swap(&u_arr[i],&u_arr[j]);	//put or swap the min value on first index and ++ index
			}
			//			u_arr[i] = min_value;
		}

	}

}

//Algorithm
//InsertionSort(A)
//A[0] = −∞
//for i = 2 to n do
//j=i
//while (A[j] < A[j − 1]) do
//swap(A[j], A[j − 1])
//j =j−1
static void insertion_sort(int u_arr[],int len) {

	int i ,j;
	for(i = 1;i<len;i++) {		//right now I have one card in my hand
		//		int key = u_arr[i];
		//		while(j>= 0 && u_arr[j] > key) {
		//			do_swap(&u_arr[j+1],&u_arr[j]);
		//			j = j-1;
		//		}
		j = i;
		while(j>=0 && u_arr[j] < u_arr[j-1]) {	//I am going to compare untill my last card in hand
			do_swap(&u_arr[j],&u_arr[j-1]);
			j = j-1;
		}

	}

}

static int get_pivot_position(int a[],int start,int end){

	int i = start;
	int pivot = start;
	for(i=start;i<=end;i++) {
		if(a[i] < a[end]) {
			do_swap(&a[pivot],&a[i]);
			pivot++;
		}
	}
	//	if(pivot != start) {
	do_swap(&a[end],&a[pivot]);
	//	}
	return(pivot);
}

static void q_rec_sort(int un_arr[],int start,int end) {

	if(start < end) {
		int pivot_position = get_pivot_position(un_arr,start,end);
		q_rec_sort(un_arr,start,pivot_position-1);
		q_rec_sort(un_arr,pivot_position+1,end);
	}
}

static void quick_sort(int un_arr[],int len) {

	//quick_sort
	//1.select pivot
	int pivot = len-1;
	int start = 0;
	q_rec_sort(un_arr,start,pivot);
}


typedef struct q{
	int data;
	struct q *next;
}queue;

static void init_q(queue *q) {
	q = (queue *) malloc(sizeof(queue));
}

static void enqueue(int data) {

}

static int dequeue(){

}

static void do_merge(int un_arr[],int start,int mid,int end) {
	printf("%d \t %d \t%d \n",start,mid,end);

	int k = 0;


}
static void rec_merge_sort(int un_arr[],int start,int end) {

	int mid = (start + end)/2;
	if(end-start > 0) {
		rec_merge_sort(un_arr,start,mid);
		rec_merge_sort(un_arr,mid+1,end);
		do_merge(un_arr,start,mid,end);
	}

}

static void merge_sort(int un_arr[],int len) {
	rec_merge_sort(un_arr,0,len-1);

}

static void kadane_algorithm(int a[],int len) {
	int i = 0;
	int max_sum = -9999;
	int sum = 0;
	int start_index = 0,max_start_index,max_end_index;
	for(i=0;i<len;i++) {
		sum += a[i];
		if(sum > max_sum) {
			max_sum = sum;
			max_start_index = start_index;
			max_end_index = i;
		}

		if(sum < 0) {
			max_sum = 0;
			start_index = i + 1;
		}
	}

	for(i=max_start_index;i<=max_end_index;i++) {
		printf("%d \t",a[i]);
	}
}

int main(int argc,char **argv) {

	int u_arr[] = {23,1,34,67,12,2,3,45,56,8,9};

	int i = 12,j=32;
	do_swap1(&i,&j);
	printf("i = %d and j = %d\t",i,j);
	printf("\n");
	for(i=0;i<11;i++) {
		printf("%d \t",u_arr[i]);
	}
	//	insertion_sort(u_arr,11);
	//	selection_sort(u_arr,11);
	//	quick_sort(u_arr,11);
	printf("\n");
	kadane_algorithm(u_arr,11);
	printf("\n");
	merge_sort(u_arr,11);
	printf("\n");
	for(i=0;i<11;i++) {
		printf("%d \t",u_arr[i]);
	}


}
