#include<stdio.h>
#include<stdlib.h>

typedef struct node {
	int data;
	struct node *right;
	struct node *left;
}bst_node;

static bst_node* make_node(int data) {
 bst_node *node;
 node = (bst_node *) malloc (sizeof(bst_node));
 node->data = data;
 node->left = NULL;
 node->right = NULL;
}


static void make_bst(bst_node **root,int data) {
   	if(*root == NULL) {
         *root = make_node(data); 
        }else {
	 if( (*root)->data < data) {
          make_bst(&(*root)->right,data); 
         } else {
          make_bst(&(*root)->left,data); 
         }	
   }
}

static void traverse_bst(bst_node *root) {
 if(root != NULL) {
  traverse_bst(root->left);
  printf("%d \t",root->data);
  traverse_bst(root->right);
 }
}

int main() {
 int input_arr[] = {12,4,34,6,8,2,14,23};
 int i = 0;
 bst_node *root = NULL;
 for(i=0;i<=7;i++) {
     make_bst(&root,input_arr[i]);
 }

 printf("\n");
 traverse_bst(root);
 printf("\n");
}
