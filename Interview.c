#include<stdio.h>

static int reverse_number(int number) {

	int rev = 0;
	int remainder = 0;		//Consider the number with 2 digits
	while(number !=0) {
		remainder = number % 10;	//Always give me last digit
		rev = rev * 10 + remainder;	//If remainder*10 then gives me original number
		number = number / 10;		//Always give me new number excluding last digit
	}

	return rev;
}

static void revrse_string(char *str) {
	if(*str)
	{
		revrse_string(str+1);  //put in the stack and then print by pop
		printf("%c", *str);
	}
}

static int count_len(char *s) {
	int cnt = 0;
	while(*s) {		//while(s) => we cannot write as as s is address while *s is content
		//Address(s) cannot be null while content(*s) null
		//In tree we root->next = NULL we are doing address NULL so we can use while(root)
		s++;
		cnt++;
	}
	return cnt;
}

void * new(){
	return "Hello";
}

static void conquer(int ip_arr[],int low,int high);
static void divide(int ip_arr[],int low,int high);

static void divide(int ip_arr[],int low,int high) {

	int mid;
	if(low >= high) {
		return;
	}
	else {
		mid = (low + high)/2;
		divide(ip_arr,low,mid);
		divide(ip_arr,mid+1,high);
		conquer(ip_arr,low,high);
	}
}


static void conquer(int ip_arr[],int low,int high) {

	int start = low;
	int rt = high;
	int mid = (low + high)/2;
	int mid_half = mid + 1;
	int lt = low;
	int target[100]; //  int *temp = (int *)malloc(sizeof(int)*array_size);

	while(lt <= mid && mid_half <= rt ) {
		if(ip_arr[lt] < ip_arr[mid_half]) {
			//copy the left portion
			target[low++] = ip_arr[lt++];
		}else {			 //else if(ip_arr[lt] > ip_arr[mid_half]){
			//copy the right portion
			target[low++] = ip_arr[mid_half++];
		}
	}


	//divide while loop in =====>  (if/else + for)
	if(lt > mid) {		//one of the while condition (lt <= mid)
		for(;mid_half <= rt;) {
			target[low++] = ip_arr[mid_half++];
		}
	}else if(mid_half > rt) {
		for(;lt <= mid;) {
			target[low++] = ip_arr[lt++];
		}
	}


	/**
	 * Copy the conquered array into original
	 */

	for(lt = start; lt<=high; lt++) {
		ip_arr[lt] = target[lt];
	}

}


static void merge_sort(int arr[],int size) {
	divide(arr,0,size-1);
}

//int main() {
//	int number = 1234;
//	int rev_number = reverse_number(number);
//	printf("%d \t",rev_number);
//
//	revrse_string("Hello");
//	number = count_len("Krishna");
//	printf("%d \t",number);
//
//	int arr[] = {10,92,8,14,22,40};
//	int i;
//	merge_sort(arr,6);
//	printf("\n");
//	for(i=0;i<=5;i++) {
//		printf("%d \t",arr[i]);
//	}
//}
