#! /bin/sh
gcc test1.c -o test_c
gcc libtest1.c -o libtest_c.so -fPIC -shared
g++ -g3 -shared -o libtest.so libtest.cpp -fPIC
g++ -g3 -shared -o libtestnew.so libtestnew.cpp -fPIC
g++ -g3 -no-pie -L$PWD -o test_no_pie test.cpp -ltest -ldl
g++ -g3 -L$PWD -o test test.cpp -ltest -ldl

