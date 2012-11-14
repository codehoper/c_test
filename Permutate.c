# include <stdio.h>

/* Function to swap values at two pointers */
void swap (char *x, char *y)
{
	char temp;
	temp = *x;
	*x = *y;
	*y = temp;
}

/* Function to print permutations of string
   This function takes three parameters:
   1. String
   2. Starting index of the string
   3. Ending index of the string. */
void permute(char *a, int i, int n)
{
	int j;
	if (i == n)
		printf("%s\n", a);
	else
	{
		for (j = i; j <= n; j++)
		{
			swap((a+i), (a+j));	//Fixing first spot/Make a[j] the start of this (sub-)permutation starting at i.
			permute(a, i+1, n);	//Find the permuations of a[i+1..n]
			swap((a+i), (a+j)); //backtrack or get original string
		}
	}
}

/* Driver program to test above functions */
int main()
{
	char a[] = "ABC";
	permute(a, 0, 2);
	getchar();
	return 0;
}
