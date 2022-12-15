CC = clang
CFLAGS = -c -g -Wall -Wpedantic -Wextra -Werror
THREADFLAG = -lpthread

all: scheduler task_generator

scheduler: scheduler.o
	$(CC) scheduler.o $(THREADFLAG) -o scheduler

scheduler.o: scheduler.c
	$(CC) $(CFLAGS) scheduler.c

task_generator: task_generator.o
	$(CC) task_generator.o $(THREADFLAG) -o task_generator

task_generator.o: task_generator.c
	$(CC) $(CFLAGS) task_generator.c

clean:
	rm -f scheduler scheduler.o task_generator task_generator.o