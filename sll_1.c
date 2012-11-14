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

void recursiveReverse(struct node** head_ref)
{
    sll* first;
    sll* rest;

    /* empty list */
    if (*head_ref == NULL)
       return;

    /* suppose first = {1, 2, 3}, rest = {2, 3} */
    first = *head_ref;
    rest  = first->next;

    /* List has only one node */
    if (rest == NULL)
       return;

    /* reverse the rest list and put the first element at the end */
    recursiveReverse(&rest);
    first->next->next  = first;

    /* tricky step -- see the diagram */
    first->next  = NULL;

    /* fix the head pointer */
    *head_ref = rest;
}

static void reverse(struct node** head_ref)
{
    sll* prev   = NULL;
    sll* current = *head_ref;
    sll* next;
    while (current != NULL)
    {
    	//Save the next pointer
        next  = current->next;
        //change/update next pointer to previous
        current->next = prev;
        //update prev to current
        prev = current;
        //update current to next
        current = next;
    }
    *head_ref = prev;
}


static void reverse_list(sll **first_ref ,sll **second_ref) {
	if(*second_ref == NULL) {
	     return;
	}else {
	  reverse_list(&(*second_ref),&(*second_ref)->next);
	}
  	sll *next_node =   (*first_ref);
    (*first_ref)->next = (*second_ref);
	(*second_ref)->next = next_node;

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
	int input_arr[] = {12,2,45,3,4};
	int i;
	for(i=0;i<5;i++) {
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
