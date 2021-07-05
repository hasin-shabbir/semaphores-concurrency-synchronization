all: chef saladmaker1 saladmaker2 saladmaker3

chef: chef.c
	gcc chef.c -o chef -lpthread

saladmaker1: saladmaker1.c
	gcc saladmaker1.c -o saladmaker1 -lpthread

saladmaker2: saladmaker2.c
	gcc saladmaker2.c -o saladmaker2 -lpthread

saladmaker3: saladmaker3.c
	gcc saladmaker3.c -o saladmaker3 -lpthread

clean:
	rm -f chef saladmaker1 saladmaker2 saladmaker3 *.o