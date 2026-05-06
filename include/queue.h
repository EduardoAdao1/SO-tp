#ifndef QUEUE_H
#define QUEUE_H

#include "defs.h"
#include <sys/time.h>

#define POLICY_FIFO 0
#define POLICY_RR 1

typedef struct node
{
    Msg pedido;
    struct timeval hora_submit;
    struct node *next;
} Node;

typedef struct queue
{
    Node *head;
    Node *tail;
    int size;
} Queue;

void init_queue(Queue *queue);
int enqueue(Queue *queue, Msg m, struct timeval t);
int dequeue(Queue *queue, Msg *m, struct timeval *t_submit);
int dequeue_policy(Queue *queue, int policy, int *last_user, Msg *m, struct timeval *t_submit);
int is_empty(Queue *queue);
void free_queue(Queue *queue);

#endif
