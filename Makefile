all:
	gcc src/a.c -lpthread && ./a.out 127.0.0.1 8000 10 100 3
