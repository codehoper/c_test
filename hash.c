/*
 * hash.c
 *
 *  Created on: Jul 16, 2012
 *      Author: vikram
 */

#include<stdio.h>
#include<stdlib.h>


//hash key->value
int get_key(int no) {

	//	int dec = no%10;
	//	int exdec = no%100;
	//	int exdec1 = no%1000;
	//	int exdec2 = no%10000;

	//	no = (((no%256)%100)%41)%10;
	//	no = (no*no)%14;
	//	return (no>2) ? no-4 : no;


	no = (((no%251)%71)%41)%13;
	no = (no*no)%23;
	return (no>2) ? no-4 : no;

}


int main(int argc,char **argv) {

	int key = get_key(256);
	printf("key for %d\n",key);

	key = get_key(248);
	printf("key for %d\n",key);

	key = get_key(240);
	printf("key for %d\n",key);

	key = get_key(331);
	printf("key for %d\n",key);

	return 1;
}
