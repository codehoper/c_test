#include<stdio.h>

int N = 5;

void swap(int *a,int *b) {

}

void swap_ref(int &a,int &b) {

}

int multiply(int *a, int fwdProduct, int indx) {
    int revProduct = 1;
    if (indx < N) {
       revProduct = multiply(a, fwdProduct*a[indx], indx+1);
       int cur = a[indx];
       a[indx] = fwdProduct * revProduct;
       revProduct *= cur;
    }
    return revProduct;
}

int main() {
	int a[] = {1,2,3,6,5};
	multiply(a,1,0);
	int i;
	for(i=0;i<5;i++) {
		printf("%d \t",a[i]);
	}
	printf("Hello world");
}
