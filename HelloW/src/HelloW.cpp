//============================================================================
// Name        : HelloW.cpp
// Author      : 
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <iostream>
#include <vector>
using namespace std;

void DoIt(int &foo, int goo);
void swap_ref(int &a,int &b) {

}

int add(int x,int y) {
	return x+y;
}

int main() {

	// int *foo, *goo;
	// foo = new int;
	// *foo = 1;
	// goo = new int;
	// *goo = 3;
	// *foo = *goo + 3;
	// foo = goo;
	// *goo = 5;
	// *foo = *goo + *foo;
	// cout << "foo" << *foo <<"\ngoo" << *goo << endl;
	// DoIt(*foo, *goo);
	// cout << (*foo) << endl;
	// cout << "foo" << *foo <<"\ngoo" << *goo << endl;
//	int *pInt = 0;
//	*pInt = 9;
//	cout << "The value at pInt: " << *pInt;
//	return 0;
//
//
//	vector<int> fst(10);
//	fst.push_back(10);
//	int t = fst.front();
//	cout << t << endl;


}

void DoIt(int &foo, int goo) {
	foo = goo + 3;
	goo = foo + 4;
	foo = goo + 3;
	goo = foo;
}

