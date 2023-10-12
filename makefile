CC = gcc
CFLAGS = -Wall -Wextra -g
OSS_PROGRAM = oss
WORKER_PROGRAM = worker

all:	$(OSS_PROGRAM)	$(WORKER_PROGRAM)

$(OSS_PROGRAM):	oss.c
	$(CC)	$(CFLAGS)	-o	$(OSS_PROGRAM)	oss.c	-lrt	-pthread

$(WORKER_PROGRAM):	worker.c
	$(CC)	$(CFLAGS)	-o	$(WORKER_PROGRAM)	worker.c	-lrt	-pthread

clean:
	rm      -f      $(OSS_PROGRAM)  $(WORKER_PROGRAM)       oss.log

.PHONY: all clean
