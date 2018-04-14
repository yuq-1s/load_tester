load_tester:
	gcc src/load_tester.c -lpthread -o load_tester

test: load_tester
	./load_tester 127.0.0.1 80 1000 2000 3
