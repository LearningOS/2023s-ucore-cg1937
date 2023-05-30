#ifndef QUEUE_H
#define QUEUE_H
#define QUEUE_SIZE (1024)

#include "types.h"

// TODO: change the queue to a priority queue sorted by priority

struct queue {
	int data[QUEUE_SIZE];
	uint64 stride[QUEUE_SIZE];
	int front;
	int tail;
	int empty;
};

void init_queue(struct queue *);
void push_queue(struct queue *, int, uint64);
int pop_queue(struct queue *);

#endif // QUEUE_H
