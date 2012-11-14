/*
 * sorts.c
 *
 *  Created on: Jul 1, 2012
 *      Author: vikram
 */


#include<stdio.h>
#include<stdlib.h>

static void swap(int* a,int* b) {
	*a = *a + *b;
	*b = *a - *b;
	*a = *a - *b;
}

static void selection_sort(int unsorted_arr[],int n) {
//Select minimum in each iteration or place
	int i;
	int j;
	int min;
	for(i=0;i<n;i++) {
		min = i;
		for(j=i+1;j<n;j++) {
			if(unsorted_arr[min] > unsorted_arr[j]) {
				min = j;
			}
		}
		if(min != i) {
			swap(&unsorted_arr[min],&unsorted_arr[i]);
		}
	}

	for(i=0;i<n;i++) {
		printf("%d \t",unsorted_arr[i]);
	}

}

void merge_array(int a[],int mid,int low,int high) {
	printf("%d \t mid = %d low = %d high = %d\n",a[0],mid,low,high);
}

void divide_array(int a[],int low,int high) {

	int mid;
	if(low < high) {
		mid = (low + high) /2;
		divide_array(a,low,mid);
		divide_array(a,mid+1,high);
		merge_array(a,mid,low,high);
	}

}


int main(int argc,char** argv){
	int un_arr[] = {3,1,4,5,7,8,3,2};
//	selection_sort(un_arr,8);
	divide_array(un_arr,0,7);
	return (0);

}
