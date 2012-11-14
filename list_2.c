/*
 * List.c
 *
 *  Created on: Jun 25, 2012
 *      Author: vikram
 */

#include<stdio.h>


void show_all_size() {
	printf("size of int %d\n",sizeof(int));
	printf("size of char %d\n",sizeof(char));
	printf("size of double %d\n",sizeof(double));
}

/**
 * Equivalent to main
 * 1.int main
 * 2.int main(void)
 * 3.int main(int argc,char** argv)
 * 4.int main(int argc,char *argv[])
 *
 */

void my_array() {
	/**
	 * 1.Array names used as pointer
	 * 2.Navigate array using pointer arithmetic
	 */
}

void my_house_and_address() {
	int my_house = 10;
	int my_address = &my_house;
	printf("my house is = %d,@ address %p\n",my_house,my_address);
}

typedef struct node{
	int payload ;
	struct node *next;
}sll;


void showAllSize();

sll* insert_one_two_three() {

	sll* head = NULL;
	sll* second = NULL;
	sll* third = NULL;

	head = (sll*)malloc(sizeof(sll));
	second = (sll*)malloc(sizeof(sll));
	third = (sll*)malloc(sizeof(sll));

	head->payload = 1;
	head->next = second;

	second->payload = 2;
	second->next = third;

	third->payload = 3;
	third->next = NULL;

	return  (head);
}



sll *head = NULL;
//void insert_node(int aPayload) {
//
//	if(head == NULL) {
//		head = (sll*)malloc(sizeof(sll));
//		head->payload = aPayload;
//		head->next = NULL;
//	}
//	else {
//		sll* new_node = (sll*)malloc(sizeof(sll));
//		new_node->payload = aPayload;
//		//Example of pointer-Assignment i.e. pointer sharing
//		new_node->next = head;
//		head = new_node;
//	}
//}




void length() {
	int cnt = 0;

}
//int main(int argc,char** argv) {
//
//	sll *head = insert_one_two_three();
//	while(head != NULL) { 	//previously it was head->next!= NULL
//		printf("%d \t",head->payload);
//		head = head->next;
//	}
//
////	show_all_size();
//
//	insert_node(1);
//	insert_node(2);
//	insert_node(3);
//
//}
