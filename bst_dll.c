#include<stdio.h>
#include <stdlib.h>
typedef struct {
	int payload;
	struct dll *next;
	struct dll *prev;
} dll;

typedef struct {
	int payload;
	struct BST *left;
	struct BST *right;
}BST;


static BST* create_tree_node(int data) {
	BST *node = (BST*) malloc(sizeof(BST));
	node->payload = data;
	node->left = NULL;
	node->right = NULL;
	return (node);
}

static void create_tree(BST **root,int adata) {
	if(*root == NULL) {
		*root = create_tree_node(adata);
	}else if((*root)->payload < adata) {
		create_tree(&(*root)->right,adata);
	}else  {
		create_tree(&(*root)->left,adata);
	}
}
static dll* create_dll(int data) {
	dll *dll_node = (dll*) malloc(sizeof(dll));
	dll_node->payload = data;
	dll_node->next = NULL;
	dll_node->prev = NULL;
	return (dll_node);
}

static void insert_dll(dll **head,int data) {
	dll *new_node = create_dll(data);
	if(*head == NULL) {
		*head = new_node;
	}else {
		new_node->next  = *head;
		(*head)->prev =  new_node;
		*head = new_node;
	}
}

static void traverse_bst(dll **head,BST *root) {
	if(root != NULL) {
		traverse_bst(head,root->left);
		insert_dll(head,root->payload);
		traverse_bst(head,root->right);
	}
}

static  void print_dll(dll *head) {
	while(head) {
		printf("%d \t",head->payload);
		head = head->next;
	}
}

static void print_inorder(BST *root) {
	if(root != NULL) {
	  print_inorder(root->left);
	  printf("%d \t",root->payload);
	  print_inorder(root->right);
	}
}
int main() {
	dll *head = NULL;
	BST *root = NULL;
	int i;
	int arr[] = {4,1,2,7,3,8,5,6};
	for(i=0;i<8;i++) {
		create_tree(&root,arr[i]);
	}
	print_inorder(root);
	traverse_bst(&head,root);
	printf("\n");
	print_dll(head);
}
