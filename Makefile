CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude
LDFLAGS =

all: folders controller runner

controller: bin/controller

runner: bin/runner

folders:
	mkdir -p src include obj bin tmp scripts

bin/controller: obj/controller.o obj/queue.o
	$(CC) $(LDFLAGS) $^ -o $@

bin/runner: obj/runner.o obj/execute.o
	$(CC) $(LDFLAGS) $^ -o $@

obj/controller.o: src/controller.c include/defs.h include/queue.h
	$(CC) $(CFLAGS) -c src/controller.c -o obj/controller.o

obj/runner.o: src/runner.c include/defs.h include/execute.h
	$(CC) $(CFLAGS) -c src/runner.c -o obj/runner.o

obj/execute.o: src/execute.c include/execute.h include/defs.h
	$(CC) $(CFLAGS) -c src/execute.c -o obj/execute.o

obj/queue.o: src/queue.c include/queue.h include/defs.h
	$(CC) $(CFLAGS) -c src/queue.c -o obj/queue.o

clean:
	rm -rf bin/* obj/* tmp/* temp/*
	rm -f Fifo_Server fifo_* log.txt out.txt input.txt erros.txt
