/*
 * rabitfibo.c
 *
 *  Created on: Jul 8, 2012
 *      Author: vikram
 */
#include<stdio.h>
#include<stdlib.h>
#define MAX 20
#define UNKNOWN -1
int F[MAX];

//static int fibo_opt(int n) {
//	int past_n;
//	int past_n_1;
//
//	fibo_opt = past_n + past_n_1;
//
//}

static int fibo_induction(int n) {

	if(F[n] != UNKNOWN) {
		return F[n];		//Base base
	}

	F[n] = fibo_induction(n-1) + fibo_induction(n-2); //recursive case
	return F[n];
}

//int main(int argc,char **argv) {
//
//	F[0] = 0;
//	F[1] = 1;
//	int i;
//	for(i=2;i<MAX;i++) {
//		F[i] = -1;
//	}
//	int n = 10;
//	int ans = fibo_induction(n);
//	printf("Fibo F[%d] = %d",n,ans);
//}




int main()
{ double i=0;
i+=0.1;
i+=0.1;
i+=0.1;
i+=0.1;
i+=0.1;
i+=0.1;
i+=0.1;
if(i==0.7)
	printf("Equal : i=0.7\n");
i+=0.1;
if(i==0.8) printf("Equal : i=0.8\n");
else printf("Not Equal : i=0.8\n");
return 0;
}
