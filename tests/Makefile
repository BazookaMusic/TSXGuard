CC=g++
CFLAGS= -std=c++0x -pthread -O3  -Wall -Werror -Wextra 

catch_main.o: catch_test_main.cpp
	$(CC) $(CFLAGS) -c $<  -o $@

tsx_test: catch_main.o tsx_test.cpp ../include/TSXGuard.hpp
	$(CC) $(CFLAGS) tsx_test.cpp catch_main.o  -o tsx_test

tests: tsx_test
	./tsx_test