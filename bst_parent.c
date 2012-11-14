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
//	struct bst *parent;
}bst;

//static void * make_node(int adata,bst *parent) {
//	bst *node = (bst *) malloc(sizeof(bst));
//	node->data = adata;
//	node->left = NULL;
//	node->right = NULL;
//	node->parent = parent;
//	return (node);
//}

static void * make_node(int adata) {
	bst *node = (bst *) malloc(sizeof(bst));
	node->data = adata;
	node->left = NULL;
	node->right = NULL;
	return (node);
}


void bfs_traversal() {

}

//void insert_data(bst **root,bst *parent,int data) {
//	if(*root == NULL) {
//		*root = make_node(data,parent);
//	}else {
//		//
//		parent = *root;
//		if((*root)->data < data) {
//			insert_data((&(*root)->right),parent,data);
//		} else if ((*root)->data > data) {
//			insert_data((&(*root)->left),parent,data);
//		}
//	}
//}

void insert_data(bst **root,int data) {
	if(*root == NULL) {
		*root = make_node(data);
	}else {
		//
		if((*root)->data < data) {
			insert_data((&(*root)->right),data);
		} else if ((*root)->data > data) {
			insert_data((&(*root)->left),data);
		}
	}
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
		 return max(1 + is_balanced_bst(root->left), 1+ is_balanced_bst(root->right));
	 }
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

static void join(bst *first,bst *second) {

	first->right = second;
	second->left = first;

}
static bst* append(bst *first_list,bst *second_list) {

	bst *first_last;
	bst *second_last;

	 if(first_list == NULL){ return (second_list);}
	 if(second_list == NULL) {return (first_list);}

	 first_last = first_list->left;
	 second_last = second_list ->right;
	 join(first_last,second_list);
	 join(second_last,first_list);

	 return(first_list);

}

static bst* convert_bst_to_dll(bst *root) {

	bst *first_list;
	bst *second_list;

	if(root == NULL) {
		return NULL;
	}

	first_list = convert_bst_to_dll(root->left);
	second_list = convert_bst_to_dll(root->right);

	//dll base case (length-1)

	root->left = root;
	root->right = root;

	first_list = append(first_list,root);
	second_list = append(first_list,second_list);

	return(first_list);

}

static void traverse_list(bst *root) {
	bst *first = root;
	while(root->right != first) {
		printf("%d \t",root->data);
		root = root->right;
	}
}

int main() {
	int arr[] = {18,10,5,12,40};
	int i;
	bst *root = NULL;
	for (i=0;i<=4;i++) {
//		insert_data(&root,NULL,arr[i]);
		insert_data(&root,arr[i]);
	}

	printf("\n");
	traverse_bst(root);
	bst *head = convert_bst_to_dll(root);
	printf("\n printing list \n");
	traverse_list(head);
//	bst *anc_node = find_common_ancestor(root,23,3);
//	printf("%d \t\n",anc_node->data);
//
//	int ht = is_balanced_bst(root);
//	printf("left ht is = %d \n",ht);
//
//	ht = min_height_bst(root);
//	printf("min ht is = %d \n",ht);


}
