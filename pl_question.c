#include<stdio.h>
#include<stdlib.h>

typedef struct ll {
	int payload;
	struct ll *next;
}sll;

static void push(sll **head_ref,int data) {

	//	create the new node,
	//	set its .next to point to the current head,
	//	and finally change the head to point to the new node.
	sll *new_node = (sll *)malloc(sizeof(sll));
	new_node->payload = data;
	new_node->next = *head_ref;
	*head_ref = new_node;
}

static void insert_payload(sll **head,int data) {

	if(*head == NULL) {
		*head = (sll *)malloc(sizeof(sll));
		(*head)->payload = data;
		(*head)->next = NULL;
	}else {
		insert_payload(&((*head)->next),data);
	}

}

static void traverse_list(sll *head) {
	printf("\n");
	while(head) {
		printf("%d \t",head->payload);
		head = head->next;
	}
}

static void just_traverse(sll **head_ref) {
	sll *first = *head_ref;
	sll *second = (*head_ref)->next;

	if(second == NULL) {
		return;
	}

	just_traverse(&(second));
	*head_ref = second;
	printf("%d \t",second->payload);

}
static void recursive_reverse(sll** head_ref)
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

	/* put the first element on the end of the list */
	recursive_reverse(&rest);
	first->next->next  = first;	//first->next->next is last node of reverse-rest list

	/* tricky step -- see the diagram */
	first->next  = NULL;

	/* fix the head pointer */
	*head_ref = rest;
}

static void rec_reverse_sll (sll **first,sll **second) {
	if(*first == NULL || *second == NULL) {
		return;
	}else {
		rec_reverse_sll(&(*second),&((*second)->next));
		(*second)->next = *first;
		//		(*first)->next = ;
	}
}

static void reverse_sll (sll **head) {
	//method of induction
	if(*head == NULL || (*head)->next == NULL) {
		return;
	}else {
		rec_reverse_sll(&(*head),&(*head)->next);
	}
}

/**
 * Move first element of second list to first element of first list
 */
static void move_node(sll **head_first,sll **head_second) {

//	Always visualize the linked list diagram when you copy the *head_second you copy the reference not all elements
//	the whole list in temp_node
//so whatever operation you perform on temp_node applicable to all list pointed by head_second
	sll *temp_node = *head_second;
	*head_second = (*head_second)->next;
	temp_node->next = NULL;

	temp_node->next = *head_first;
	*head_first = temp_node;

}
static void front_back_split_fast(sll *source,sll **front_ref,sll **back_ref){
	//instead of traversing the whole list and determine count do simple thing
	//use 2 pointer fast=>move twice slow=>move only one position

	sll *fast_ptr,*slow_ptr;
	if(source == NULL || source->next == NULL) {
		*front_ref = source;
		*back_ref = NULL;
	}else {
		fast_ptr = source->next;
		slow_ptr = source;
		while(fast_ptr) {
			fast_ptr = fast_ptr->next;
			if(fast_ptr) {
				slow_ptr = slow_ptr->next;
				fast_ptr = fast_ptr->next;
			}
		}
		*front_ref = source;
		*back_ref = slow_ptr->next;
		slow_ptr->next = NULL;
	}


}

static void front_back_split(sll *source,sll **front_ref,sll **back_ref) {

	int count = 0;
	int count1,count2;
	sll *temp = source;
	//traverse list
	while(temp){
		count++;
		temp = temp->next;
	}
	if(count % 2 == 0) {
		count1 = count/2;
	}else {
		count1 = (count/2) + 1;
	}
	count2 = count -count1;
	sll *temp2;
	while(count1) {
		count1--;
		if(*front_ref == NULL) {
			*front_ref = source;
			temp2 = *front_ref;
		}else {
			temp2 = temp2->next;
		}
		source = source->next;
	}
	temp2->next = NULL;
	while(count2){
		count2--;
		if(*back_ref == NULL) {
			*back_ref = source;
		}
	}
	temp2 = NULL;
}

static void detect_cycle(sll *head) {

	int found_cycle = 0;
	sll *hare = head;			//Initially both run at same speed
	sll *tortoise = head;

	while(hare) {
		hare = hare->next;		//hare runs twice as tortoise
		if(hare) {
			hare = hare->next;
			tortoise = tortoise->next;
			if(hare == tortoise) {
				found_cycle = 1;
				break;
			}
		}

	}

	if(found_cycle == 1) {
		printf("\nfound cycle \n");
	}
	else {
		printf("\nnot found cycle \n");
	}

}

static void insert_cycle(sll **head) {

	sll *temp_head = *head;
	while(temp_head->next) {
		temp_head = temp_head->next;
	}
	temp_head->next = (*head)->next;
}

int main(int argc,char **argv){

	sll *head = NULL;
	int i = 0;
	for(i=1;i<=4;i++) {
		insert_payload(&head,i);
	}
	traverse_list(head);
	//	reverse_sll(&head);
	recursive_reverse(&head);
	//	printf("\n");
	traverse_list(head);
	printf("\n");
	//	just_traverse(&head);
	sll *head2 = NULL;
	for(i=1;i<=3;i++) {
		insert_payload(&head2,i);
	}
	push(&head2,34);
	push(&(head2->next),45);
	push(&(head2->next),48);
	push(&(head2->next),52);
	traverse_list(head2);
	sll *front = NULL;
	sll *back = NULL;
	//	front_back_split(head2,&front,&back);
	front_back_split_fast(head2,&front,&back);
	traverse_list(front);
	traverse_list(back);

	sll *a=NULL,*b=NULL;
	for(i=1;i<=3;i++) {
		insert_payload(&a,i);
		insert_payload(&b,i);
	}
	traverse_list(a);
	traverse_list(b);
	move_node(&a,&b);
	traverse_list(a);
	traverse_list(b);

	sll *cycle = NULL;
	for(i=1;i<=6;i++) {
		insert_payload(&cycle,i);
	}
	insert_cycle(&cycle);
	detect_cycle(cycle);

}
