#include<stdio.h>
#include<stdlib.h>
#define FOUND 0
#define NOT_FOUND 1
#define SUCESSS 0
#define FAILURE 1
/**
 *	Binary search tree
 *	operations
 *	1.Insert
 *	2.Delete
 *	3.Search
 *	4.Traverse
 *	All operation takes O(h) time (h is ht of BST
 *	For perfectly balanced tree h = ln(n) n : total no of nodes in BST)
 */

struct tree_node {
	int payload;
	struct tree_node *left;
	struct tree_node *right;
};

typedef struct tree_node bst;

static bst* create_node(int a_payload){
	bst *branch_node = (bst*)malloc(sizeof(bst));
	branch_node->payload = a_payload;
	branch_node->left = NULL;
	branch_node->right = NULL;
	return(branch_node);
}
/*
 * Insert BST
 */
//** => to change something we pass as reference
/**
 * You can copy the reference into another var but cannot change var
 *
 *
 */
static void insert_tree_node(bst **root_ref,bst* node) {
	bst *current;
	current = *root_ref;
	if(current == NULL) {
		*root_ref = node;
	}else {
		if(current->payload < node->payload) {
			insert_tree_node(&(current->right),node);
		}
		else if(current->payload > node->payload) {
			insert_tree_node(&(current->left),node);
		}
	}
}

static void pre_order(bst *root) {
	//D-L-R
	if(root == NULL) {
		return;
	}
	printf("%d \t",root->payload);
	pre_order(root->left);
	pre_order(root->right);
}
/**
 * Travrse BST
 */
static void post_order(bst *root) {
	//L-R-D
	if(root == NULL) {
		return;
	}
	post_order(root->left);
	post_order(root->right);
	printf("%d \t",root->payload);
}
static void in_order(bst *root) {
	//L-D-R
	if(root == NULL) {
		return;
	}
	in_order(root->left);
	printf("%d \t",root->payload);
	in_order(root->right);
}


static int search_bst(bst *root,int key) {

	if(NULL == root) {
		return NOT_FOUND;
	}
	if(key == root->payload) {
		return FOUND;
	}
	if(key > root->payload) {
		//traverse in right direction
		search_bst(root->right,key);
	}else {
		//traverse in left direction
		search_bst(root->left,key);
	}
}

static void delete_bst_node(bst** root,int key) {
	bst *temp_node;
	temp_node = *root;
	if(temp_node == NULL) {
		return;
	}
	else if(temp_node->payload == key) {
		if(temp_node->left == NULL && temp_node->right == NULL) {
			*root =(*root)->left;
		}
		else if(temp_node->left == NULL) {
			*root = (*root)->right;
		}
		else if(temp_node->right == NULL) {
			*root = (*root)->left;
		}
		else if(temp_node->right != NULL && temp_node->left != NULL) {
			//The Best example of pointer assignment
			//(*test_node) = (*test_node)->right; vs (*test_node)->right = (*test_node)->right->left;
			bst *succ_node = *root;
			//			(*root)->right = (*root)-> right->left;
			succ_node = succ_node->right;
			while(succ_node->left != NULL) {
				succ_node = succ_node->left;
			}
			(*root)->payload = succ_node->payload;
			printf("test = %d\n",succ_node->payload);

			//			*root = (*root)->left;
			//			while((*root)->left != NULL) {
			//				*root = (*root)->left;
			//			}
			//If you want to change the value use the double pointer i.e. Reference
			//If you want to traverse list use single pointer
			bst** node_ref;
			bst* node_t = *root;
			node_ref = &(node_t)->right;
			node_t = *node_ref;
			while(node_t->left != NULL) {
				node_ref = &(node_t)->left;
				node_t = *node_ref;
			}
			*node_ref = (*node_ref)->left;
			//			node_t = node_t->left;


			/**
			 * Failed try
//			 */
			//			bst** node_ref;
			//			node_ref = &(*root)->right;
			//			while((*root)->left != NULL) {
			//				node_ref = &(*root)->left;
			//				*root = *node_ref;
			//			}
			//			*node_ref = (*node_ref)->left;


		}
	}
	else if(temp_node->payload < key) {
		delete_bst_node(&(temp_node)->right,key);
	}
	else if(temp_node->payload > key){
		delete_bst_node(&(temp_node)->left,key);
	}
}

/**
 * When you pass by value you send whole new copy lasts only untill function ends
 * remember c:local scope,stack
 */
static int delete_node(bst **root,int key) {

	bst *temp_node = *root;
	if(search_bst(temp_node,key) == FOUND) {
		while((*root)->payload != key) {
			if((*root)->payload < key) {
				(*root) = (*root)->right;
			}
			else {
				*root = (*root)->left;
			}
		}

		if( NULL == (*root)->left && NULL == (*root)->right) {
			//			bst **temp_node1 = &temp_node;
			//			*temp_node1 = (*temp_node1)->left;
			//			temp_node->payload = temp_node->left->payload;
			//			free(*temp_node1);
			*root = (*root)->left;
			printf("\n");
		}
		else if(NULL == temp_node->left) {
			temp_node = temp_node->right;
		}
		else if(NULL == temp_node->right) {
			temp_node = temp_node->left;
		}
		else if(NULL != temp_node->right && NULL != temp_node->left) {
			//if nodes have both children replace with successor
			//			temp_node = temp_node->right;
			//			while(temp_node ->left != NULL) {
			//				temp_node = temp_node ->left;
			//			}

			bst *succ_node = temp_node -> right;
			while(succ_node->left != NULL) {
				succ_node = succ_node->left;
			}
			temp_node = succ_node;
		}
		return SUCESSS;
	}else {
		return FAILURE;
	}

}

static void ref_check(int *a,int *b) {
	int temp = a;
	int temp1 = *a;
	*a = 12;
	a = 23;
	temp = 10;
	temp1 = 34;
	//	int temp_r = a;
	//	int *temp_ref = &temp_r;
	//	*temp_ref = 33;
	//	*test = 45;
	//	printf("\n tt =%d",*test);
}
void find_rec_max_depth(bst *root,int count) {
	int maxCount = -999;
	if(root == NULL) {
		return ;
	}
	//	count += 1;
	count++;
	if(root->left == NULL && root->right == NULL){
		printf("count :: %d\n",count);
		if(maxCount < count) {
			printf("max count %d",maxCount);
			maxCount = count;
		}
	}
	find_rec_max_depth(root->left,count);
	//	count += 1;
	find_rec_max_depth(root->right,count);
	//	count += 1;				did not worked
	//	count = count +1;
	//	return (count);
}

void find_max_depth(bst *root) {

	find_rec_max_depth(root,0);
	//printf("max depth count %d\n",depth_count);
}

void print_all_path(int path[],int len) {
	int i = 0;
	int sum = 0;
	for(i=0;i<len;i++) {
		printf("%d-",path[i]);
		sum += path[i];
	}
	printf("\nSum = %d",sum);
	printf("\n");
}
void path_trv_rec(int path[],bst *node,int len) {

	if(node == NULL) {
		return;
	}
	path[len] = node->payload;
	len++;
	//if leaf node
	if(node->left == NULL && node->right == NULL) {
		print_all_path(path,len);
	}else {
		path_trv_rec(path,node->left,len);
		path_trv_rec(path,node->right,len);
	}


}
void print_path(bst *node) {
	int path[100];
	path_trv_rec(path,node,0);
}

int has_path_sum(bst *root) {
	int sum = 0;
	if(root == NULL) {
		return (0);
	}

	while(root != NULL) {
		sum = has_path_sum(root->left);
		sum = has_path_sum(root->right);
		sum = sum + root->payload;
		return sum;
	}

}

/**
 * Mirror cross-check :
 * Before mirror traverse in-order => ascending
 * After mirror traverse in-order => descending
 */
static void make_mirror(bst *node) {
	bst *temp_node;
	if(node == NULL ) {
		return;
	}
	else if(node->left != NULL || node->right != NULL) {

		temp_node = node->left;
		node->left = node->right;
		node->right = temp_node;
		make_mirror(node->left);
		make_mirror(node->right);
	}
}

static void replace_element(bst *node) {

	if(node->payload == 17) {
		node->payload = 19;
	}
}

static void double_bst(bst *node) {

	bst *temp_node;
	if(node == NULL) {
		return;
	}

	//	if(node->left != NULL || node->right !=NULL) {
	temp_node = create_node(node->payload);
	temp_node->left = node->left;		//pointer assignment sharing
	node->left = temp_node;
	double_bst(node->left->left);		//To avoid recursive-ness at left
	double_bst(node->right);
	//	}

}


static int is_bst(bst *node) {
	int cmp = 0;
	int mid;
	int min;
	int max;
	if(node == NULL) {
		return (0);
	}
	//	while(node != NULL) {
	if((node->left != NULL || node->right != NULL)) {
		/**
		 * Return Recursion syntax
		 * 0.If condition
		 * 1.recursion ==>[put all things in stack]
		 * 2.operation ==>[draw one by one in LIFO]
		 */
		//1.recursion
		cmp = is_bst(node->left);
		cmp = is_bst(node->right);

		//2.operation
		mid = node->payload;
		if(node->left == NULL) {
			min = -9999;
		}else {
			min = node->left->payload;
		}
		if(node->right == NULL) {
			max = 9999;
		}else {
			max = node->right->payload;
		}
		if(mid < max && mid > min) {
			cmp = 1;
		}
		else {
			cmp = 0;
		}
//		printf("==%d==",cmp);
		return (cmp);
	}

}

int main(int argc,char **argv) {
	bst *root = NULL;
	int a=9,b=10;
	int i =10;
	ref_check(&a,&b);
	printf("ans = %d\n",a);

	int arr[] = {18,8,12,2,16,15,14,4,3,5,10};

	for (i=0;i<10;i++) {
		bst* node = create_node(arr[i]);
		insert_tree_node(&root,node);
	}

	find_max_depth(root);
	//	//	printf("\n");
	//	pre_order(root);
	//	printf("\n");
	//	post_order(root);
	//	printf("\n");
	//	in_order(root);
	//	printf("\n");
	//	int found_ = search_bst(root,77);
	//	printf("status %d \n",found_);
	in_order(root);
	printf("\n");
	int sum = has_path_sum(root);
	printf("sum %d\n",sum);

	print_path(root);
	//	delete_node(&root,5);
	//	delete_bst_node(&root,5);
	//	delete_bst_node(&root,1);
	delete_bst_node(&root,12);
	in_order(root);
	printf("\n");

	bst* test_root = NULL;
	bst* node;
	//	int arr1[] = {4,5,2,3,1};
	int arr1[] = {17,5,25,7,9,8};
	for(i=0;i<=5;i++) {
		node = create_node(arr1[i]);
		insert_tree_node(&test_root,node);
	}
	in_order(test_root);
	printf("\n");
	int bst__ = is_bst(test_root);
	printf("\t %d==>",bst__);
	//	make_mirror(test_root);
	//	in_order(test_root);
	//	printf("\n");
	//	replace_element(test_root);
	//	in_order(test_root);
	//	double_bst(test_root);
	//	in_order(test_root);
	printf("\n");
	return (0);
}
