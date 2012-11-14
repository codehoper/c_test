#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include "stack.h"
#define ERROR -999
#define SUCCESS 0

struct stack_ds {
	int top;
	int piles[20];
};
typedef struct stack_ds stack;

static int is_empty(stack *stack_ptr);
static int is_full(stack *stack_ptr);
static void push(stack** stack_ref,int payload);
static int pop(stack** stack_ref);

static int is_empty(stack *stack_ptr) {
	if(stack_ptr->top == 0){
		return (ERROR);
	}
	return (SUCCESS);
}

static int is_full(stack *stack_ptr) {
	if(stack_ptr->top == 20) {
		return (ERROR);
	}
	return (SUCCESS);
}

static void push(stack** stack_ref,int payload) {
	if(is_full(*stack_ref) == SUCCESS) {
		(*stack_ref)->piles[(*stack_ref)->top] = payload;
		(*stack_ref)->top = (*stack_ref)->top + 1;
	}else {
		printf("stack is full \n");
	}

}

static int pop(stack** stack_ref) {
	int payload;
	if(is_empty(*stack_ref) == SUCCESS) {
		(*stack_ref)->top = (*stack_ref)->top - 1;
		payload = (*stack_ref)->piles[(*stack_ref)->top];
		return (payload);
	}else {
		printf("stack is empty \n");
	}
}

//int main(int argc,char** argv) {
//	stack* stack_1 = (stack*) malloc(sizeof(stack));
//	memset(stack_1,0,sizeof(stack));
//	push(&stack_1,10);
//	push(&stack_1,12);
//	push(&stack_1,13);
//	push(&stack_1,14);
//	push(&stack_1,15);
//	int payload = pop(&stack_1);
//	printf("data popped %d \n",payload);
//	payload = pop(&stack_1);
//	printf("data popped %d \n",payload);
//	return(0);
//}
