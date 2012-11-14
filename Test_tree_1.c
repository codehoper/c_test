#include<stdio.h>
#include<stdlib.h>
/**
 * N-ary tree
 */

struct list {
	int data;
	struct list *next;
};
typedef struct list sll;
struct tree_node {
	int data;
	sll *child;
	struct tree_node *node;
};

typedef struct tree_node tree;

tree* make_tree_node(int data) {

	tree* tree_node = (tree *) malloc(sizeof(tree_node));
	tree_node->child = (sll *) malloc(sizeof(sll));
	tree_node->data = data;
	tree_node->node = NULL;
	return(tree_node);
}


void insert_tree_node(tree **root,int data) {

	if(*root == NULL) {
		*root = make_tree_node(data);
	}else {
		insert_tree_node(&((*root)->node),data);
	}


}

int main(int argc, char **argv) {

	tree* root = NULL;
    insert_tree_node(&root,10);

}
