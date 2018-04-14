all:
	gcc src/load_tester.c -lpthread && ./a.out 127.0.0.1 80 10 100 3
