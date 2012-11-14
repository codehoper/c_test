/*
 * bst.c
 *
 *  Created on: Sep 29, 2012
 *      Author: vikram
 */


/*
 * bst_parent.c
 *
 *  Created on: Sep 24, 2012
 *      Author: vikram
 */


#include<stdio.h>
#include<stdlib.h>
typedef struct bst {

	int data;
	struct bst *left;
	struct bst *right;
	struct bst *parent;
}bst;

static void * make_node(int adata,bst *parent) {
	bst *node = (bst *) malloc(sizeof(bst));
	node->data = adata;
	node->left = NULL;
	node->right = NULL;
	node->parent = parent;
	return (node);
}
#define MAX 20
typedef struct q {
	bst *piles[MAX];
	int count;
	int head;
	int tail;
}queue;

void init(queue **q) {
	*q = (queue *)malloc(sizeof(queue));
	(*q)->count = 1;
	(*q)->tail  = 1;
	(*q)->head  = 1;
}
void insert_q (queue *q,bst *node) {
	q->count += 1;
	q->piles[q->tail] = node;
	q->tail = (q->tail + 1) % MAX;
}

int is_empty(queue *q) {
	if(q->count < 1) {
		return 1;
	}return 0;
}

bst* pop_q(queue *q) {
	bst *node;
	q->count -= 1;
	node = q->piles[q->head];
	q->head += 1;
	return (node);
}



void bfs_traversal(bst *root) {

	if(root == NULL) {
		return;
	}
	queue *q = NULL;
	init(&q);
	insert_q(q,root);
	bst *node = root;
	while(is_empty(q) != 1) {
		node = pop_q(q);
		if(node) {
			printf("%d \t", node->data);
			insert_q(q,node->left);
			insert_q(q,node->right);
		}

	}


}

static bst* make_node_bt(int data) {
	bst *node = (bst*) malloc(sizeof(bst));
	node->data = data;
	node->left = NULL;
	node->right = NULL;
	return (node);
}
static void insert_bt_data(bst **root,int data,int flip) {

	bst *node;
	node = *root;
	if(*root == NULL) {
		*root = make_node_bt(data);
	}else {
		if(flip == 0) {
			insert_bt_data(&(*root)->left,data,flip);
		}else if (flip == 1) {
			insert_bt_data(&(*root)->right,data,flip);
		}
	}
}
void insert_data(bst **root,bst *parent,int data) {
	if(*root == NULL) {
		*root = make_node(data,parent);
	}else {
		//
		parent = *root;
		if((*root)->data < data) {
			insert_data((&(*root)->right),parent,data);
		} else if ((*root)->data > data) {
			insert_data((&(*root)->left),parent,data);
		}
	}
}


static bst* get_max_next_element(bst *node,bst *root) {

	bst *max_node;
	max_node = NULL;
	if(node->right == NULL) {
		max_node = node->parent;
		while(max_node->data <= node->data && max_node != root) {
			max_node = node->parent;
		}
	}else {
		max_node = node->right;
	}

	return max_node; //for the rigthmost node returns null

}

static int min(int a,int b) {
	if(a < b) {
		return a;
	}else {
		return b;
	}
}
static int max(int a,int b) {
	if(a > b) {
		return a;
	}else {
		return b;
	}

}
int is_balanced_bst(bst *root) {

	if(root == NULL) {
		return 0;
	}else {
		return 1 + is_balanced_bst(root->left);
	}
}

int min_height_bst(bst *root) {

	if(root == NULL) {
		return 0;
	}else {
		return max(1 + is_balanced_bst(root->left), 1 + is_balanced_bst(root->right));
	}
}

int bst_verfiy(bst *root,int min,int max) {
	if(root == NULL) {
		return (1);
	}
	else if(root->data > min && root ->data < max) {
		return (bst_verfiy(root->left,min,root->data) && bst_verfiy(root->right,root->data,max) && 1);
	}else {
		return 0;
	}
}

int is_bst(bst *root){

	return (bst_verfiy(root,-999,999));
}

bst*  find_common_ancestor(bst *root,int value1,int value2) {

	bst* node = root;
	while(1) {
		if(value1 < root->data && value2 < root->data) {
			node = find_common_ancestor(root->left,value1,value2);
		}else if (value1 > root->data && value2 > root->data) {
			node = find_common_ancestor(root->right,value1,value2);
		}
		return node;
	}
}

void traverse_bst(bst *root) {

	if(root) {
		traverse_bst(root->left);
		//		if(root->parent != NULL) {
		//			printf("%d with parent pointer %d\n",root->data,root->parent->data);
		//		}else {
		//			printf("%d \n",root->data);
		//		}
		printf("%d \t",root->data);
		traverse_bst(root->right);
	}

}

static void join(bst *first_list,bst *second_list) {
	//No return only join
	first_list->right = second_list;		//consider the one node in dll i.e. set the next of first and previous of second
	second_list->left = first_list;

}
static bst* append(bst *first_list,bst *second_list) {

	bst *first_first;
	bst *second_first;

	if(first_list == NULL){ return (second_list);}
	if(second_list == NULL) {return (first_list);}

	//consider left as first element in the list so you have to perform the operation on first element
	first_first = first_list->left;		//smallest element
	second_first = second_list ->left;	//largest element

	join(first_first,second_list);		//to maintain DLL i.e. smallest ->biggest list->

	join(second_first,first_list);		//biggest -> smallest list i.e. smallest->biggest list->biggest ->smallest list

	return(first_list);				//we wanted to show ascending so first_list i.e. smallest element first

}



int main() {
	int arr[] = {18,10,5,12,40};
	int i;
	bst *root = NULL;
	for (i=0;i<=4;i++) {
		insert_data(&root,NULL,arr[i]);
	}


	printf("\n");
	traverse_bst(root);
	bst *anc_node = find_common_ancestor(root,10,5);
	printf(" \nthe anc data %d \t\n",anc_node->data);

	bst *node = get_max_next_element(anc_node,root);
	printf("The next max element is %d",node->data);
	printf("\n");
	bfs_traversal(root);
	int bt = is_bst(root);
	printf("\n the bst value :%d \n",bt);

	bst *root1 = NULL;
	for (i=0;i<=4;i++) {
		insert_bt_data(&root1,arr[i],i % 2);
	}

	bt = is_bst(root1);
	printf("\n the bst value :%d \n",bt);

	printf("\n");
	bfs_traversal(root1);

	//	int ht = is_balanced_bst(root);
	//	printf("left ht is = %d \n",ht);
	//
	//	ht = min_height_bst(root);
	//	printf("min ht is = %d \n",ht);


}
