/*
 * bits.c
 *
 *  Created on: Aug 6, 2012
 *      Author: vikram
 */

#include<stdio.h>
#include<stdlib.h>
#include<limits.h>

static void print_bin(int n) {
	int s = sizeof(int) * 8;
	while (s) {
		if (n & 1)
			printf("1");
		else
			printf("0");
		n >>= 1;
		s--;
	}
	printf("\n");
}

static void detect_opp_sign() {
	int x= 34 , y = -23;               // input values to compare signs
	print_bin(x);
	print_bin(y);
	if ((x ^ y) < 0) { // true iff x and y have opposite signs
		printf("opposite sign \n");
	} else {
		printf("not a opposite sign \n");
	}
}
static void do_rev () {
	unsigned int v = 219;     // input bits to be reversed
	print_bin(v);
	unsigned int r = v; // r will be reversed bits of v; first get LSB of v
	int s = sizeof(v) * CHAR_BIT - 1; // extra shift needed at end
//
//	for (v >>= 1; v; v >>= 1)
//	{
//	  r <<= 1;
//	  r |= v & 1;
//	  s--;
//	}
	while(v) {
		v >>= 1;		//TODO:Use this method for rotation (n rotation) n times or O.M. use left_shift | right_shift(K-n)
						//K = total bits and n = no of rotation.
		r <<= 1;
		r |= v & 1;	//get only one bit of v every time/
		s--;
	}

	r <<= s; // shift when v's highest bits are zero
	printf("%d \n",r);
	print_bin(r);
}
static void do_and() {
	int a = 13;
	printf ("max_int = %d size of int = %d\n",INT_MIN,sizeof(int));
	int b = 23;
	unsigned int t = 65535;
//	printf("unsigned int = %d and signed int = %d\n",~t,~a);
	int c = a & b;
	printf("And = %d \n",c);
	c = a | b;
	printf("Oring = %d \n",c);
	c = a << 1;
	printf("a Left shift by 1 = %d \n",c);
	c = a >> 1;
	printf("a = %d Right shift by 1 = %d \n",a,c);
}

int store = 0;
void more_simple_assign(int number) {

	int shifted_number = 1 << number;
	store |= shifted_number;

}


static void verify_number(int number) {

	printf("\n");
	int tester = store;
	tester = tester >> number;
	tester = tester & 1;
	if(tester == 1) {
		printf("found \n");
	} else {
		printf("not found \n");
	}

}

void assign_number(int number) {



	int a = 0x1F;
//	int a =  31;
//	printf("%d \n",a);
	int b = a << number;
	int twos_cmplmt = -b;
	int t = twos_cmplmt & b;
	t = t >> number;
	t = t & 0x01;
//	t = t & 1;
	if( t == 1) {
		printf(" equal ");
	} else {
		printf("not equal");
	}


	//OR more simple
//	int store = 0;
//	a = 0x0F;
////	int mask = number & a;
//	int temp = 1 << mask;
//	store |= temp;
}


int main(int argc,char **argv) {
//	do_and();
//	do_rev();
//	detect_opp_sign();

	assign_number(12);
	int i;
	int t[] = {10,23,24,28,32};
	for (i=0;i<5;i++) {
		more_simple_assign(t[i]);
	}
	verify_number(10);
	verify_number(11);
	verify_number(28);

}
