#include <stdlib.h>
#include <queue.h>
// Função que processa uma mensagem nova
void enqueue(Node **queue, Msg m, struct timeval t) {
    // Criamos um novo nodo da queue com essa mensagem e a sua hora de submit
    Node *new_node = malloc(sizeof(Node));
    new_node->pedido = m;
    new_node->hora_submit = t;
    new_node->next = NULL;
    // Se a queue for vazia adicionamos o novo node ao topo, se não adicionamos no fim
    if (*queue == NULL) {
        *queue = new_node;
    } else {
        Node *temp = *queue;
        while (temp->next != NULL) temp = temp->next;
        temp->next = new_node;
    }
}

// Função que retira um nodo da queue
Msg dequeue(Node **queue, struct timeval *t_submit) {
    // Se a queue estiver vazia não à nada a retirar
    if (*queue == NULL) {
        Msg empty = {0};
        return empty;
    }
    // Se a queue não estiver vazia vamos devolver a mensagem à cabeça e guardar o tempo de submit num ponteiro para usar no controller
    Node *temp = *queue;
    Msg m = temp->pedido;
    if (t_submit) *t_submit = temp->hora_submit;
    
    *queue = temp->next;
    free(temp);
    return m;
}

// Função que verifica se a queue está vazia
int is_empty(Node *queue) {
    return queue == NULL;
}