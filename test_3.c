#include<stdio.h>

int is_prime(int number){
	int div = number;
	div --;
	while(div > 1){
		if(number%div == 0){
			return 1;
		}
	div --;
	}
	return 0;
}

int convert_string_to_int(const char* str) {
	int cnt = 0;
	int multiplier = 1;
	int number = 0;
	while(*str != '\0' ) {
		cnt ++;
		str++; //update str is important just consider as str path/number just ++/--
	}
	while(cnt != 0) {
		char t = *(--str);  //update str is important
		int n = t - '0';
		number += (multiplier *n);
		multiplier *= 10;
		cnt --;
	}
 return number;
}

int main(int argc,char **argv)
{
	int ret;
	ret = is_prime(2);
	if(ret == 0){
		printf("\n number is  prime \n");
	}else 	{
		printf("\n number is not  prime\n");
	}
	int no = convert_string_to_int("156");
	printf("the converted number is %d",no);
}


