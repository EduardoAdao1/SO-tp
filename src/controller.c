#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>

#include "defs.h"
#include "queue.h"

Inicio *em_exec = NULL;

Queue fila_espera;

int max_comandos = 1;

int comandos_ativos = 0;

int shutdown_pedido = 0;

pid_t shutdown_cliente_pid = -1;

int politica_escalonamento = POLICY_FIFO;

int ultimo_user_rr = -1;

int escrever_tudo(int fd, const void *buffer, size_t tamanho)
{
    const char *ptr = buffer;
    size_t escritos = 0;

    while (escritos < tamanho)
    {
        ssize_t n = write(fd, ptr + escritos, tamanho - escritos);

        if (n == -1)
        {
            if (errno == EINTR)
                continue;

            return 0;
        }

        if (n == 0)
            return 0;

        escritos += (size_t)n;
    }

    return 1;
}

int escrever_formatado(int fd, const char *formato, ...)
{
    char buffer[1024];
    va_list args;

    va_start(args, formato);
    int n = vsnprintf(buffer, sizeof(buffer), formato, args);
    va_end(args);

    if (n < 0)
        return 0;

    if (n >= (int)sizeof(buffer))
        n = (int)sizeof(buffer) - 1;

    return escrever_tudo(fd, buffer, (size_t)n);
}

void limpar_filhos()
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

void mostrar_uso()
{
    escrever_formatado(STDERR_FILENO, "Uso: ./controller <parallel-commands> <sched-policy>\n");
    escrever_formatado(STDERR_FILENO, "sched-policy: fifo | rr | round-robin\n");
}

int configurar_politica(char *policy)
{
    if (strcmp(policy, "fifo") == 0)
    {
        politica_escalonamento = POLICY_FIFO;
        return 1;
    }

    if (strcmp(policy, "rr") == 0 || strcmp(policy, "round-robin") == 0)
    {
        politica_escalonamento = POLICY_RR;
        return 1;
    }

    return 0;
}

int enviar_inteiro_fifo(pid_t pid, int valor)
{
    char fifo_resposta[50];

    snprintf(fifo_resposta, sizeof(fifo_resposta), "fifo_%d", pid);

    // O modo não bloqueante impede o controller de ficar preso se o FIFO privado já não existir.
    int resposta_fd = open(fifo_resposta, O_WRONLY | O_NONBLOCK);

    if (resposta_fd == -1)
        return 0;

    int ok = escrever_tudo(resposta_fd, &valor, sizeof(int));

    close(resposta_fd);

    return ok;
}

int autorizar(pid_t pid)
{
    return enviar_inteiro_fifo(pid, RESP_AUTHORIZED);
}

int rejeitar(pid_t pid)
{
    return enviar_inteiro_fifo(pid, RESP_REJECTED);
}

void notificar_shutdown()
{
    if (shutdown_cliente_pid != -1)
        enviar_inteiro_fifo(shutdown_cliente_pid, RESP_SHUTDOWN_DONE);
}

int registar_exec(Msg *msg, struct timeval t)
{
    Inicio *n = malloc(sizeof(Inicio));

    if (n == NULL)
        return 0;

    n->pid = msg->pid;
    n->user_id = msg->user_id;
    n->command_id = msg->command_id;
    n->t_submit = t;

    strncpy(n->command, msg->command, MAX_COMMAND - 1);
    n->command[MAX_COMMAND - 1] = '\0';

    n->next = em_exec;
    em_exec = n;

    return 1;
}

int remove_exec(Msg *msg, struct timeval *t_submit)
{
    Inicio *curr = em_exec;
    Inicio *prev = NULL;

    while (curr)
    {
        if (curr->pid == msg->pid)
        {
            if (t_submit != NULL)
                *t_submit = curr->t_submit;

            msg->user_id = curr->user_id;
            msg->command_id = curr->command_id;

            strncpy(msg->command, curr->command, MAX_COMMAND - 1);
            msg->command[MAX_COMMAND - 1] = '\0';

            if (prev)
                prev->next = curr->next;
            else
                em_exec = curr->next;

            free(curr);

            return 1;
        }

        prev = curr;
        curr = curr->next;
    }

    return 0;
}

int iniciar_comando(Msg *msg, struct timeval t)
{
    if (registar_exec(msg, t) == 0)
    {
        rejeitar(msg->pid);
        return 0;
    }

    if (autorizar(msg->pid) == 0)
    {
        struct timeval ignorado;
        remove_exec(msg, &ignorado);
        return 0;
    }

    comandos_ativos++;

    if (politica_escalonamento == POLICY_RR)
        ultimo_user_rr = msg->user_id;

    return 1;
}

void escrever_log(Msg m, struct timeval t)
{
    struct timeval fim;

    gettimeofday(&fim, NULL);

    long ms = (fim.tv_sec - t.tv_sec) * 1000 + (fim.tv_usec - t.tv_usec) / 1000;

    int fd = open("log.txt", O_WRONLY | O_APPEND | O_CREAT, 0666);

    if (fd == -1)
        return;

    escrever_formatado(
        fd,
        "User- %d | Command-id- %d | Cmd- %s | Duração- %ldms\n",
        m.user_id,
        m.command_id,
        m.command,
        ms);

    close(fd);
}

int copiar_fila(Queue *destino, Queue *origem)
{
    init_queue(destino);

    Node *curr = origem->head;

    while (curr)
    {
        if (enqueue(destino, curr->pedido, curr->hora_submit) == 0)
        {
            free_queue(destino);
            return 0;
        }

        curr = curr->next;
    }

    return 1;
}

int enviar_fila_fifo(int fd)
{
    Node *espera = fila_espera.head;

    while (espera)
    {
        if (escrever_formatado(
                fd,
                "user-id %d - command-id %d\n",
                espera->pedido.user_id,
                espera->pedido.command_id) == 0)
            return 0;

        espera = espera->next;
    }

    return 1;
}

int enviar_fila_ordem_escalonamento(int fd)
{
    if (politica_escalonamento == POLICY_FIFO)
        return enviar_fila_fifo(fd);

    Queue copia;

    if (copiar_fila(&copia, &fila_espera) == 0)
        return enviar_fila_fifo(fd);

    int ultimo_local = ultimo_user_rr;
    Msg prox;
    struct timeval tempo;

    while (dequeue_policy(&copia, politica_escalonamento, &ultimo_local, &prox, &tempo))
    {
        if (escrever_formatado(
                fd,
                "user-id %d - command-id %d\n",
                prox.user_id,
                prox.command_id) == 0)
        {
            free_queue(&copia);
            return 0;
        }
    }

    free_queue(&copia);

    return 1;
}

void enviar_status(pid_t cliente_pid)
{
    char fifo_path[50];

    snprintf(fifo_path, sizeof(fifo_path), "fifo_%d", cliente_pid);

    int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);

    if (fd == -1)
        return;

    if (escrever_formatado(fd, "---\nExecuting\n") == 0)
    {
        close(fd);
        return;
    }

    Inicio *curr = em_exec;

    while (curr)
    {
        if (escrever_formatado(
                fd,
                "user-id %d - command-id %d\n",
                curr->user_id,
                curr->command_id) == 0)
        {
            close(fd);
            return;
        }

        curr = curr->next;
    }

    if (escrever_formatado(fd, "---\nScheduled\n") == 0)
    {
        close(fd);
        return;
    }

    // A fila em espera é apresentada pela ordem real de escalonamento.
    enviar_fila_ordem_escalonamento(fd);

    close(fd);
}

void tratar_status(pid_t cliente_pid)
{
    pid_t filho = fork();

    if (filho == 0)
    {
        // O filho responde ao status para o controller continuar a receber pedidos.
        enviar_status(cliente_pid);
        _exit(0);
    }

    if (filho == -1)
    {
        enviar_status(cliente_pid);
    }
}

void tentar_lancar_proximos()
{
    while (!is_empty(&fila_espera) && comandos_ativos < max_comandos)
    {
        Msg prox;
        struct timeval tempo;

        if (dequeue_policy(&fila_espera, politica_escalonamento, &ultimo_user_rr, &prox, &tempo) == 0)
            return;

        iniciar_comando(&prox, tempo);
    }
}

void free_exec()
{
    Inicio *curr = em_exec;

    while (curr)
    {
        Inicio *tmp = curr;
        curr = curr->next;
        free(tmp);
    }

    em_exec = NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        mostrar_uso();
        return 1;
    }

    max_comandos = atoi(argv[1]);

    if (max_comandos <= 0)
    {
        escrever_formatado(STDERR_FILENO, "parallel-commands deve ser maior que 0\n");
        mostrar_uso();
        return 1;
    }

    if (configurar_politica(argv[2]) == 0)
    {
        escrever_formatado(STDERR_FILENO, "politica invalida: %s\n", argv[2]);
        mostrar_uso();
        return 1;
    }

    init_queue(&fila_espera);

    unlink(SERVER);

    if (mkfifo(SERVER, 0666) == -1)
    {
        perror("mkfifo");
        return 1;
    }

    // Abrir em O_RDWR evita bloqueios quando ainda não há writers no FIFO.
    int server_fd = open(SERVER, O_RDWR);

    if (server_fd == -1)
    {
        perror("open");
        unlink(SERVER);
        return 1;
    }

    Msg msg;
    ssize_t bytes_read;

    while (1)
    {
        limpar_filhos();

        bytes_read = read(server_fd, &msg, sizeof(Msg));

        if (bytes_read == -1)
        {
            if (errno == EINTR)
                continue;

            perror("read");
            break;
        }

        if (bytes_read != sizeof(Msg))
            continue;

        struct timeval agora;

        gettimeofday(&agora, NULL);

        if (msg.type == TYPE_EXEC)
        {
            // Após shutdown, novos comandos são rejeitados para evitar runners bloqueados.
            if (shutdown_pedido)
            {
                rejeitar(msg.pid);
                continue;
            }

            if (comandos_ativos < max_comandos)
            {
                iniciar_comando(&msg, agora);
            }
            else
            {
                if (enqueue(&fila_espera, msg, agora) == 0)
                    rejeitar(msg.pid);
            }
        }

        else if (msg.type == TYPE_DONE)
        {
            struct timeval t;

            if (remove_exec(&msg, &t) == 1)
            {
                escrever_log(msg, t);

                if (comandos_ativos > 0)
                    comandos_ativos--;

                tentar_lancar_proximos();
            }

            if (shutdown_pedido && comandos_ativos == 0 && is_empty(&fila_espera))
                break;
        }

        else if (msg.type == TYPE_STATUS)
        {
            tratar_status(msg.pid);
        }

        else if (msg.type == TYPE_STOP)
        {
            if (shutdown_cliente_pid == -1)
                shutdown_cliente_pid = msg.pid;
            else
                enviar_inteiro_fifo(msg.pid, RESP_SHUTDOWN_DONE);

            shutdown_pedido = 1;

            if (comandos_ativos == 0 && is_empty(&fila_espera))
                break;
        }
    }

    notificar_shutdown();

    limpar_filhos();

    close(server_fd);

    unlink(SERVER);

    free_queue(&fila_espera);
    free_exec();

    return 0;
}
