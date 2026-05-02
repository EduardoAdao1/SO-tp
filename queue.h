#include <defs.h>
#include <sys/time.h>

typedef struct node{
    Msg pedido;
    struct timeval hora_submit;
    struct node *next;
} Node;

void enqueue(Node **queue,Msg m,struct timeval t);
Msg dequeue(Node **queue,struct timeval *t_submit);
int is_empty(Node *queue);