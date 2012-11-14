#make file for the myhttpd

myhttpd: myhttpd.o argparser.o 
	gcc -o myhttpd myhttpd.o argparser.o -lpthread
	
argparser.o: argparser.c argparser.h
	gcc -c -Wall argparser.c 

myhttpd.o: myhttpd.c myhttpd.h
	gcc -c -Wall myhttpd.c -lpthread
	

clean:
	rm myhttpd *.o
