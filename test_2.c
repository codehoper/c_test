#include<stdio.h>

typedef struct binary_tree {

	struct binary_tree *left;
	struct binary_tree *right;
	struct binary_tree *parent;
	int data;
}bt;


bt* create_node(int data) {
	bt *node = (bt *) malloc(sizeof(bt));
	node->data = data;
	node->left = NULL;
	node->right = NULL;
	node->parent = NULL;
	return node;
}

void populate_bt(bt **root,int data,bt *parent) {

	bt *temp = *root;
	if((*root) == NULL) {
		*root = create_node(data);
		if(parent != NULL) {
			(*root)->parent = parent;
		}
	}else {
		if(temp->left) {

		}

	}


}

int main() {

}
