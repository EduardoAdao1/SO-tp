#include <stdlib.h>
#include "queue.h"

// Inicializa a fila de pedidos
void init_queue(Queue *queue)
{
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
}

// Insere um pedido no fim da fila
int enqueue(Queue *queue, Msg m, struct timeval t)
{
    // Criamos um novo nó da fila com o pedido e a hora de submissão
    Node *new_node = malloc(sizeof(Node));

    if (new_node == NULL)
        return 0;

    new_node->pedido = m;
    new_node->hora_submit = t;
    new_node->next = NULL;

    // Se a fila estiver vazia, o novo nó passa a ser o início e o fim
    if (queue->head == NULL)
    {
        queue->head = new_node;
        queue->tail = new_node;
    }
    else
    {
        // Se a fila não estiver vazia, adicionamos o novo nó ao fim
        queue->tail->next = new_node;
        queue->tail = new_node;
    }

    queue->size++;

    return 1;
}

// Remove o primeiro pedido da fila
int dequeue(Queue *queue, Msg *m, struct timeval *t_submit)
{
    // Se a fila estiver vazia não há nada a remover
    if (queue->head == NULL)
        return 0;

    // Guardamos o primeiro nó da fila
    Node *temp = queue->head;

    // Copiamos os dados do pedido removido
    if (m != NULL)
        *m = temp->pedido;

    if (t_submit != NULL)
        *t_submit = temp->hora_submit;

    // Avançamos o início da fila
    queue->head = temp->next;

    // Se a fila ficou vazia, o fim também passa a NULL
    if (queue->head == NULL)
        queue->tail = NULL;

    free(temp);

    queue->size--;

    return 1;
}

// Verifica se a fila está vazia
int is_empty(Queue *queue)
{
    return queue->head == NULL;
}

// Liberta a memória usada pela fila
void free_queue(Queue *queue)
{
    Node *curr = queue->head;

    while (curr != NULL)
    {
        Node *temp = curr;
        curr = curr->next;
        free(temp);
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
}