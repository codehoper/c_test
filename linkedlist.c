#include<stdio.h>
#include<stdlib.h>

struct node {
	struct node *next;
	int payload;
};

struct skip_node {
	struct node *next[2];
	int payload;
};
typedef struct node sll;
typedef struct skip_node skip;


skip* make_new_skip_node(int a_payload,int level) {
	skip *new_node = (skip *) malloc(sizeof(skip));
	new_node->payload = a_payload;
	int i;
	for (i=0;i<level;i++) {
		new_node->next[i] = NULL;
	}
	return (new_node);
}

void insert_skip_node(skip **headRef,int a_payload) {
	skip *current_ptr = *headRef;
	if (current_ptr == NULL) {
		*headRef = make_new_skip_node(a_payload,2);
	} else {
		skip *new_node = make_new_skip_node(a_payload,2);
		new_node->next[0] = *headRef;
		*headRef = new_node;
	}
}
/**
 * Create node
 */
sll* make_new_node(int a_payload) {
	sll *new_node = (sll *) malloc(sizeof(sll));
	new_node->payload = a_payload;
	new_node->next = NULL;
	return (new_node);
}

void append_node(sll **headRef, int a_payload) {

	sll *current_ptr = *headRef;
	if (current_ptr == NULL) {
		*headRef = make_new_node(a_payload);
	} else {
		append_node(&(current_ptr->next), a_payload);
	}

}
/**
 *  list1 and list2, suppose they are sorted in ascending order, h
 *  ow to check whether each element in list 2
 */

void verify_list_nodes(sll *head1, sll *head2) {
	/**
	 * options
	 * 1.O(n^2) naive approach
	 * 2.Convert 1 list to BST and then verify each element
	 * NOTE : For BST you have to select the middle element or create Balanced BST
	 * 3.Use skip list
	 */

}

void prepend_node(sll **headRef, int a_payload) {

	sll *new_node = malloc(sizeof(sll));
	new_node->payload = a_payload;
	new_node->next = NULL;

	if (*headRef == NULL) {
		*headRef = new_node;
	} else {
		new_node->next = *headRef;
		*headRef = new_node;
	}
}

void print_all_node(sll *head_ptr) {

	sll *current_ptr = NULL;
	current_ptr = head_ptr;
	printf("\n");
	while (current_ptr != NULL) {
		printf("%d \t", current_ptr->payload);
		current_ptr = current_ptr->next;
	}

}

void print_all_nodes_level(skip *head_ptr,int level) {

	skip *current_ptr = NULL;
	current_ptr = head_ptr;
	printf("\n");
	while (current_ptr != NULL) {
		printf("%d \t", current_ptr->payload);
		current_ptr = current_ptr->next[level];
	}

}



static void use_dummy_node() {
	sll dummy_node;
	sll *tail = &dummy_node;
	dummy_node.next = NULL;
	int i;
	for (i = 0; i < 5; i++) {
		prepend_node(&(tail->next), i);
		tail = tail->next;
	}
	print_all_node(dummy_node.next);

}

static void count_nodes(sll *head) {
	//Use iteration technique to count the no of nodes in the list
	sll *current = NULL;
	current = head;
	int count = 0;
	while (current != NULL) {
		count++;
		current = current->next; //****Advance the pointer ******
	}
	printf("Length of list = %d \n", count);
}

static void get_N_th(sll *head, int n) {
	sll *current = NULL;
	current = head;
	int cnt = n;
	while (n > 0 && current != NULL) {
		current = current->next;
		n--;
	}
	if (current == NULL || n < 0) {
		printf("no payload found\n");
	} else {
		printf("payload found = %d @dist = %d\n", current->payload, cnt);
	}
}

static void delete_list(sll **head) {
	sll *current = NULL;
	sll *next = NULL;
	current = *head;
	while (current != NULL) {
		next = current->next;
		free(current); //for freeing memory we pass pointer (which is allocated/pointing to the block of memory)
		current = next;
	}
	//	free(*head);
	//	*head = NULL;
}
static void insert_N_pos(sll **head, int a_payload, int n) {
	sll *new_node = (sll*) malloc(sizeof(sll));
	new_node->payload = a_payload;
	new_node->next = NULL;
	sll *current_ptr = *head;
	if (current_ptr == NULL) {
		*head = new_node;
	}
	if (n == 0) {
		new_node->next = *head;
		*head = new_node;
	} else {
		while (n > 1 && current_ptr != NULL) {
			n--;
			current_ptr = current_ptr->next;
		}
		//if it is in the middle
		new_node->next = current_ptr->next;
		current_ptr->next = new_node;
	}
}

static void put_node_at_head(sll **head, int start, int no) {

	sll *current = NULL;
	current = *head;
	int i = 0;
	while (start > 1) {
		start--;
		current = current->next;
	}
	for (i = 0; i < no; i++) {
		sll *next_head = current->next;
		current->next = next_head->next;
		next_head->next = *head;
		*head = next_head;
	}

}

sll* reverse_list(sll **head) {
	sll *current = *head;
	sll *extra_ptr = NULL;
	sll *temp = NULL;

	if (current == NULL || current->next == NULL) {
		return (*head);
	} else {
		while (current != NULL) {
			temp = current;
			current = current->next;
			temp->next = extra_ptr;
			extra_ptr = temp;
			/**
			 * next = current->next;
			 * current->next = result;
			 * result = current;
			 * current = next;
			 */
		}
		*head = extra_ptr;
	}
	return (*head);
}

static void pop_node(sll **head) {
	sll* current = NULL;
	current = *head;
	if (current == NULL) {
		printf("list is empty \n");
	} else {
		*head = current->next;
		printf("Popped element =%d\n", current->payload);
		free(current);
		//For freeing memory we need pointer
		//however for updating the head we need reference to pointer
	}
}

static void use_head_push() {
	sll *head = NULL;
	prepend_node(&head, 1);
	prepend_node(&head, 2);
	prepend_node(&head, 3);
	prepend_node(&head, 4);
	count_nodes(head);
	get_N_th(head, 2);
	//	pop_node(&head);
	insert_N_pos(&head, 23, 0);
	print_all_node(head);
	put_node_at_head(&head, 3, 2);
	print_all_node(head);
	sll *_temp = reverse_list(&head);
	print_all_node(_temp);
	//	delete_list(&head);
	//	print_all_node(head);

}

int main(int argc, char **argv) {

	//	use_head_push();

	//	sll *head1 = NULL;
	//	int i = 0;
	//	for(i=0;i<4;i++) {
	//		append_node(&head1,i);
	//	}
	//
	//
	//	print_all_node(head1);

	sll* head1 = NULL;
	append_node(&head1, 4);
	append_node(&head1, 6);
	append_node(&head1, 15);
	append_node(&head1, 22);
	append_node(&head1, 45);
	append_node(&head1, 46);

	sll *head2 = NULL;
	append_node(&head2, 2);
	append_node(&head2, 5);
	append_node(&head2, 15);
	append_node(&head2, 25);
	append_node(&head2, 45);
	append_node(&head2, 48);

	print_all_node(head1);
	verify_list_nodes(head1,head2);

	skip *head3 = NULL;
	insert_skip_node(&head3,21);
	insert_skip_node(&head3,16);
	print_all_nodes_level(head3,0);

	return (0);

}
