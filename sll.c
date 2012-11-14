#include<stdio.h>
#include<stdlib.h>

typedef struct sll {
 int data;
 struct sll *next;
}sll;

/***
Protypes for linked list
***/
static void create_sll(sll **head_ref,int data);
static sll* make_node_ll(int data);

static void create_sll(sll **head_ref,int data) {
	sll *new_node = make_node_ll(data);	
	if(*head_ref == NULL) {
		*head_ref = new_node;
	}else {
	   	new_node->next = *head_ref;
		*head_ref = new_node;
	}
}

static void reverse_list(sll **first_ref ,sll **second_ref) {
	if(*second_ref == NULL) {
	     return;
	}else {
	  reverse_list(&(*second_ref),&(*second_ref)->next);	
	}
	(*second_ref)->next = *first_ref;
	(*first_ref)->next = NULL;
}

static sll* make_node_ll(int data) {
	sll* sll_node;
	sll_node = (sll *)malloc(sizeof(sll));
	sll_node->data = data;
	sll_node->next = NULL;
	return (sll_node);
}

static void print_all_nodes(sll *head) {
	while(head != NULL) {
		printf("%d \t",head->data);
		head = head->next;
	}
}

int main() {
	sll *head = NULL;
	int input_arr[] = {12,2,45,3,4,6,7};
	int i;
	for(i=0;i<7;i++) {
		create_sll(&head,input_arr[i]);
	}
	printf("\n");
	print_all_nodes(head);
	printf("\n");
	reverse_list(&head,&(head->next));
	printf("\n");
	print_all_nodes(head);
	printf("\n");
}

