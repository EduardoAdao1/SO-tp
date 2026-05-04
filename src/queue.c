#include <stdlib.h>
#include "queue.h"

void init_queue(Queue *queue)
{
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
}

int enqueue(Queue *queue, Msg m, struct timeval t)
{
    Node *new_node = malloc(sizeof(Node));

    if (new_node == NULL)
        return 0;

    new_node->pedido = m;
    new_node->hora_submit = t;
    new_node->next = NULL;

    if (queue->head == NULL)
    {
        queue->head = new_node;
        queue->tail = new_node;
    }
    else
    {
        queue->tail->next = new_node;
        queue->tail = new_node;
    }

    queue->size++;

    return 1;
}

static int remove_node(Queue *queue, Node *prev, Node *curr, Msg *m, struct timeval *t_submit)
{
    if (curr == NULL)
        return 0;

    if (m != NULL)
        *m = curr->pedido;

    if (t_submit != NULL)
        *t_submit = curr->hora_submit;

    if (prev != NULL)
        prev->next = curr->next;
    else
        queue->head = curr->next;

    if (queue->tail == curr)
        queue->tail = prev;

    free(curr);

    queue->size--;

    if (queue->size == 0)
    {
        queue->head = NULL;
        queue->tail = NULL;
    }

    return 1;
}

int dequeue(Queue *queue, Msg *m, struct timeval *t_submit)
{
    if (queue->head == NULL)
        return 0;

    return remove_node(queue, NULL, queue->head, m, t_submit);
}

static int dequeue_round_robin(Queue *queue, int *last_user, Msg *m, struct timeval *t_submit)
{
    if (queue->head == NULL)
        return 0;

    Node *curr = queue->head;
    Node *prev = NULL;

    while (curr != NULL)
    {
        if (*last_user == -1 || curr->pedido.user_id != *last_user)
        {
            int user = curr->pedido.user_id;
            int ok = remove_node(queue, prev, curr, m, t_submit);

            if (ok)
                *last_user = user;

            return ok;
        }

        prev = curr;
        curr = curr->next;
    }

    // Se só houver pedidos do mesmo utilizador, o RR cai para FIFO.
    int ok = dequeue(queue, m, t_submit);

    if (ok && m != NULL)
        *last_user = m->user_id;

    return ok;
}

int dequeue_policy(Queue *queue, int policy, int *last_user, Msg *m, struct timeval *t_submit)
{
    if (policy == POLICY_RR)
        return dequeue_round_robin(queue, last_user, m, t_submit);

    return dequeue(queue, m, t_submit);
}

int is_empty(Queue *queue)
{
    return queue->head == NULL;
}

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
