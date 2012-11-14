//============================================================================
// Name        : HelloW.cpp
// Author      : 
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <iostream>
using std::cout;
using std::endl;

int add(int x, int y) { return x+y; }
int sub(int x, int y) { return x-y; }

int main() {
    // define fp to be a pointer to a function which takes
    // two int parameters and returns an int
    int (*fp)(int, int);

    fp = &add;
    cout << fp(6,3) << endl; // get 9
    fp = &sub;
    cout << fp(6,3) << endl; // get 3

    return 0;
}
