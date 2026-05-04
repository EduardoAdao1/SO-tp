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

// Lista de comandos que já foram autorizados e estão em execução
Inicio *em_exec = NULL;

// Fila de comandos que ainda estão à espera de autorização
Queue fila_espera;

// Número máximo de comandos que podem executar ao mesmo tempo
int max_comandos = 1;

// Número de comandos atualmente em execução
int comandos_ativos = 0;

// Indica se já foi pedido o encerramento do controlador
int shutdown_pedido = 0;

// Guarda o pid do runner que pediu o encerramento do controlador
pid_t shutdown_cliente_pid = -1;

// Escreve todos os bytes pedidos para um descritor
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

        escritos += n;
    }

    return 1;
}

// Escreve texto formatado para um descritor
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
        n = sizeof(buffer) - 1;

    return escrever_tudo(fd, buffer, n);
}

// Limpa processos filhos que já terminaram
void limpar_filhos()
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

// Envia um inteiro para o FIFO privado de um runner
int enviar_inteiro_fifo(pid_t pid, int valor)
{
    // FIFO privado do runner, criado com base no pid do processo runner
    char fifo_resposta[50];

    snprintf(fifo_resposta, sizeof(fifo_resposta), "fifo_%d", pid);

    // Abrimos em modo não bloqueante para o controlador não ficar preso
    int resposta_fd = open(fifo_resposta, O_WRONLY | O_NONBLOCK);

    if (resposta_fd == -1)
        return 0;

    // Enviamos o valor indicado para o runner
    int ok = escrever_tudo(resposta_fd, &valor, sizeof(int));

    close(resposta_fd);

    return ok;
}

// Função que dá autorização ao runner para correr o comando
int autorizar(pid_t pid)
{
    // O valor 1 indica que o runner pode executar o comando
    return enviar_inteiro_fifo(pid, 1);
}

// Notifica o runner que pediu shutdown de que o controlador vai encerrar
void notificar_shutdown()
{
    if (shutdown_cliente_pid != -1)
        enviar_inteiro_fifo(shutdown_cliente_pid, 1);
}

// Regista um comando na lista de comandos em execução
int registar_exec(Msg *msg, struct timeval t)
{
    Inicio *n = malloc(sizeof(Inicio));

    if (n == NULL)
        return 0;

    // Guardamos a informação necessária para status e log
    n->pid = msg->pid;
    n->user_id = msg->user_id;
    n->command_id = msg->command_id;
    n->t_submit = t;

    // Copiamos o comando de forma segura, garantindo o terminador '\0'
    strncpy(n->command, msg->command, MAX_COMMAND - 1);
    n->command[MAX_COMMAND - 1] = '\0';

    // Inserimos no início da lista de comandos em execução
    n->next = em_exec;
    em_exec = n;

    return 1;
}

// Remove um comando da lista de comandos em execução quando este termina
// Também recupera a informação guardada para ser usada no log
int remove_exec(Msg *msg, struct timeval *t_submit)
{
    Inicio *curr = em_exec;
    Inicio *prev = NULL;

    while (curr)
    {
        // O pid identifica o runner que terminou o comando
        if (curr->pid == msg->pid)
        {
            // Recuperamos o tempo em que o comando foi submetido
            if (t_submit != NULL)
                *t_submit = curr->t_submit;

            // Recuperamos os dados do comando para escrever no log
            msg->user_id = curr->user_id;
            msg->command_id = curr->command_id;

            strncpy(msg->command, curr->command, MAX_COMMAND - 1);
            msg->command[MAX_COMMAND - 1] = '\0';

            // Removemos o nó da lista ligada
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

    // Se não encontrou o comando, retorna 0
    return 0;
}

// Inicia a execução de um comando, registando-o e autorizando o runner
int iniciar_comando(Msg *msg, struct timeval t)
{
    // Primeiro registamos o comando para o controller já o conhecer quando o runner responder com DONE
    if (registar_exec(msg, t) == 0)
        return 0;

    // Só depois enviamos a autorização para o runner começar a execução
    if (autorizar(msg->pid) == 0)
    {
        struct timeval ignorado;
        remove_exec(msg, &ignorado);
        return 0;
    }

    comandos_ativos++;

    return 1;
}

// Escreve no ficheiro de log a informação de um comando terminado
void escrever_log(Msg m, struct timeval t)
{
    struct timeval fim;

    gettimeofday(&fim, NULL);

    // Calcula a duração total desde a submissão até ao fim do comando
    long ms = (fim.tv_sec - t.tv_sec) * 1000 + (fim.tv_usec - t.tv_usec) / 1000;

    // Abre o ficheiro de log em modo append, criando-o caso ainda não exista
    int fd = open("log.txt", O_WRONLY | O_APPEND | O_CREAT, 0666);

    if (fd == -1)
        return;

    // Escrevemos os identificadores do utilizador, do comando e a sua duração
    escrever_formatado(
        fd,
        "User- %d | Command-id- %d | Cmd- %s | Duração- %ldms\n",
        m.user_id,
        m.command_id,
        m.command,
        ms);

    close(fd);
}

// Envia ao runner a lista de comandos em execução e em espera
void enviar_status(pid_t cliente_pid)
{
    char fifo_path[50];

    snprintf(fifo_path, sizeof(fifo_path), "fifo_%d", cliente_pid);

    // Abrimos o FIFO privado do runner que pediu o status em modo não bloqueante
    int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);

    if (fd == -1)
        return;

    // Envia a secção dos comandos em execução
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

    // Envia a secção dos comandos em espera
    if (escrever_formatado(fd, "---\nScheduled\n") == 0)
    {
        close(fd);
        return;
    }

    Node *espera = fila_espera.head;

    while (espera)
    {
        if (escrever_formatado(
                fd,
                "user-id %d - command-id %d\n",
                espera->pedido.user_id,
                espera->pedido.command_id) == 0)
        {
            close(fd);
            return;
        }

        espera = espera->next;
    }

    close(fd);
}

// Cria um processo filho para responder ao pedido de status
void tratar_status(pid_t cliente_pid)
{
    pid_t filho = fork();

    if (filho == 0)
    {
        // O filho envia o status para não bloquear o ciclo principal do controller
        enviar_status(cliente_pid);
        _exit(0);
    }

    if (filho == -1)
    {
        // Se o fork falhar, respondemos no processo principal como fallback
        enviar_status(cliente_pid);
    }
}

// Tenta lançar comandos que estejam na fila de espera
// Esta função é chamada sempre que fica disponível uma vaga de execução
void tentar_lancar_proximos()
{
    while (!is_empty(&fila_espera) && comandos_ativos < max_comandos)
    {
        Msg prox;
        struct timeval tempo;

        // Remove o próximo comando da fila de espera
        if (dequeue(&fila_espera, &prox, &tempo) == 0)
            return;

        // Passa o comando para execução e autoriza o runner respetivo
        iniciar_comando(&prox, tempo);
    }
}

// Liberta a memória usada pela lista de comandos em execução
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
    // Recebe o número máximo de comandos que podem executar em paralelo
    if (argc > 1)
        max_comandos = atoi(argv[1]);

    // Garante que o número de comandos paralelos é sempre válido
    if (max_comandos <= 0)
        max_comandos = 1;

    // Inicializa a fila de comandos em espera
    init_queue(&fila_espera);

    // Cria o FIFO principal do servidor
    // Se o FIFO já existir, não é considerado erro fatal
    if (mkfifo(SERVER, 0666) == -1 && errno != EEXIST)
    {
        perror("mkfifo");
        return 1;
    }

    // Abrimos o FIFO do servidor em leitura e escrita para evitar bloqueios indesejados
    int server_fd = open(SERVER, O_RDWR);

    if (server_fd == -1)
    {
        perror("open");
        unlink(SERVER);
        return 1;
    }

    Msg msg;
    ssize_t bytes_read;

    // Ciclo principal do controlador
    while (1)
    {
        // Limpamos filhos terminados para evitar processos zombie
        limpar_filhos();

        // Lê pedidos enviados pelos runners
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

        // Guarda o momento em que o pedido chegou ao controlador
        gettimeofday(&agora, NULL);

        if (msg.type == TYPE_EXEC)
        {
            // Se o shutdown já foi pedido, não aceitamos novos comandos
            if (shutdown_pedido)
                continue;

            // Se ainda houver vaga, o comando é autorizado imediatamente
            if (comandos_ativos < max_comandos)
            {
                if (iniciar_comando(&msg, agora) == 0)
                    perror("iniciar_comando");
            }
            else
            {
                // Se não houver vaga, o pedido fica em espera
                if (enqueue(&fila_espera, msg, agora) == 0)
                    perror("enqueue");
            }
        }

        else if (msg.type == TYPE_DONE)
        {
            struct timeval t;

            // Remove o comando da lista de execução e recupera a informação original
            if (remove_exec(&msg, &t) == 1)
            {
                escrever_log(msg, t);

                if (comandos_ativos > 0)
                    comandos_ativos--;

                // Como terminou um comando, pode haver vaga para outro
                tentar_lancar_proximos();
            }

            // Se já foi pedido shutdown e não há mais comandos, encerramos
            if (shutdown_pedido && comandos_ativos == 0 && is_empty(&fila_espera))
                break;
        }

        else if (msg.type == TYPE_STATUS)
        {
            // Envia ao runner a lista de comandos em execução e em espera
            tratar_status(msg.pid);
        }

        else if (msg.type == TYPE_STOP)
        {
            // Guardamos o runner que deve ser avisado quando o controller encerrar
            if (shutdown_cliente_pid == -1)
                shutdown_cliente_pid = msg.pid;

            // O controlador não encerra imediatamente se ainda houver comandos ativos ou em espera
            shutdown_pedido = 1;

            if (comandos_ativos == 0 && is_empty(&fila_espera))
                break;
        }
    }

    // Antes de encerrar, avisamos o runner que pediu o shutdown
    notificar_shutdown();

    // Limpamos filhos que possam ter terminado entretanto
    limpar_filhos();

    close(server_fd);

    // Remove o FIFO principal do servidor
    unlink(SERVER);

    // Liberta memória antes de terminar
    free_queue(&fila_espera);
    free_exec();

    return 0;
}