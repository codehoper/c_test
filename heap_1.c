/*
 * heap.c
 *
 *  Created on: Aug 4, 2012
 *      Author: vikram
 */

static int get_parent(int i) {

	return (i/2);
}


static int get_left_child(int i) {

	return (2 * i + 1);
}

static int get_right_child(int i) {

	return (2 * i + 2);
}

//TODO :Above functions can be used as MACRO or INLINE function

static void do_swap(int *a,int *b) {
	int temp = *a+*b;
	*a = temp - *a;
	*b = temp - *a;
}

static void max_heapify(int a[],int i,int len) {
	int left = get_left_child(i);
	int right = get_right_child(i);
	int largest = i;
	if(left<=len && right<=len) {
		if(a[i] < a[left]) {
			largest = left;
		}
		if(a[largest] < a[right]) {
			largest = right;
		}
	}
	if(largest != i) {
		do_swap(&a[largest],&a[i]);
		i = largest;
		max_heapify(a,i,len);
	}

}

static void build_max_heap(int arr_[],int len) {
	int i = len/2 - 1;
	for(;i>=0;i--) {
		max_heapify(arr_,i,len);
	}
}
int main(int argc,char **argv) {

	int i = 10;
	int arr_[] = {3,23,4,21,1,4,45,67,34,22,12,14,8,5,55};
	printf("\n");
	for(i = 0;i<15;i++) {
		printf("%d \t",arr_[i]);
	}

	build_max_heap(arr_,15);
	printf("\n");
	for(i = 0;i<15;i++) {
		printf("%d \t",arr_[i]);
	}
}
