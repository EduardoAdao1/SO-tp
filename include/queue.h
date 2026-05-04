#ifndef QUEUE_H
#define QUEUE_H

#include "defs.h"
#include <sys/time.h>

// Estrutura de um nó da fila de pedidos
typedef struct node
{
    Msg pedido;                 // Pedido/comando guardado na fila
    struct timeval hora_submit; // Hora em que o comando foi submetido
    struct node *next;          // Próximo nó da fila
} Node;

// Estrutura da fila de pedidos
typedef struct queue
{
    Node *head; // Início da fila
    Node *tail; // Fim da fila
    int size;   // Número de pedidos na fila
} Queue;

// Inicializa a fila
void init_queue(Queue *queue);

// Insere um pedido no fim da fila
int enqueue(Queue *queue, Msg m, struct timeval t);

// Remove o primeiro pedido da fila
int dequeue(Queue *queue, Msg *m, struct timeval *t_submit);

// Verifica se a fila está vazia
int is_empty(Queue *queue);

// Liberta a memória usada pela fila
void free_queue(Queue *queue);

#endif