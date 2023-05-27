#include "queue.h"
#include "defs.h"

void init_queue(struct queue *q)
{
	q->front = q->tail = 0;
	q->empty = 1;
}

void push_queue(struct queue *q, int value, uint64 stride)
{
	if (!q->empty && q->front == q->tail) {
		panic("queue shouldn't be overflow");
	}
	q->empty = 0;
	q->data[q->tail] = value;
	q->stride[q->tail] = stride;
	q->tail = (q->tail + 1) % NPROC;
}

int pop_queue(struct queue *q)
{
	if (q->empty)
		return -1;
	int curr_value = q->data[q->front];
	uint64 curr_stride = q->stride[q->front];
	int min_stride_idx = q->front;
	for (int i = q->front ; i != q->tail; i = (i + 1) % NPROC) {
		if (q->stride[i] < curr_stride) {
			curr_value = q->data[i];
			curr_stride = q->stride[i];
			min_stride_idx = i;
		}
	}
	for (int i = min_stride_idx; i != q->front; i = (i - 1 + NPROC) % NPROC) {
		q->data[i] = q->data[(i - 1 + NPROC) % NPROC];
		q->stride[i] = q->stride[(i - 1 + NPROC) % NPROC];
	}
	q->front = (q->front + 1) % NPROC;
	if (q->front == q->tail)
		q->empty = 1;
	return curr_value;
}
