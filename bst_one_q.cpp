#include<iostream>
#include <queue>

typedef struct Node {
	int val;
	struct Node *left;
	struct Node *right;
}Node;


void levelOrder_two_q(Node *root)
{
     printf("\nlevel order : ");
     if(root)
     {
             std::queue<Node*> aQueue, bQueue;
             std::queue<Node*> &currentQueue = aQueue;
             std::queue<Node*> &childQueue = bQueue;
             currentQueue.push(root);

             while(!currentQueue.empty())
             {
                   root = currentQueue.front();
                   printf("%d ", root->val);
                   currentQueue.pop();
                   if(root->left)
                            childQueue.push(root->left);
                   if(root->right)
                            childQueue.push(root->right);

                   if(currentQueue.empty())
                   {
                         printf("\n");
                         swap(currentQueue, childQueue);
                   }
             }
     }
}
/**
 * with single queue
 */
void levelOrder(Node *root)
{
	printf("\nlevel order : ");
	if(root)
	{
		std::queue<Node*> currentQueue;
		int currentLevelNodes = 0, nextLevelNodes = 0;

		currentQueue.push(root);
		currentLevelNodes++;

		while(!currentQueue.empty())
		{
			root = currentQueue.front();
			printf("%d ", root->val);
			currentQueue.pop();
			currentLevelNodes--;
			if(root->left)
			{
				currentQueue.push(root->left);
				nextLevelNodes++;
			}
			if(root->right)
			{
				currentQueue.push(root->right);
				nextLevelNodes++;
			}

			if(!currentLevelNodes)
			{
				printf("\n");
				currentLevelNodes = nextLevelNodes;
				nextLevelNodes = 0;
			}
		}
	}
}


