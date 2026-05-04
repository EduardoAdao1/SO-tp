#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "defs.h"
#include "execute.h"

#define MAX_WAIT_ITER 500

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

void nome_fifo_privado(char *buffer, size_t tamanho, pid_t pid)
{
    snprintf(buffer, tamanho, "fifo_%d", pid);
}

void fechar_fifo_privado(int fifo_fd, char *fifo_path)
{
    close(fifo_fd);
    unlink(fifo_path);
}

int criar_fifo_privado(char *fifo_path, size_t tamanho)
{
    pid_t pid = getpid();

    nome_fifo_privado(fifo_path, tamanho, pid);

    // Evita reutilizar um FIFO antigo.
    unlink(fifo_path);

    if (mkfifo(fifo_path, 0666) == -1)
        return -1;

    // O modo não bloqueante evita que o runner fique preso à espera do controller.
    int fd = open(fifo_path, O_RDONLY | O_NONBLOCK);

    return fd;
}

int enviar_mensagem(Msg *msg)
{
    // Se o controller não estiver ativo, o runner falha em vez de bloquear.
    int fd = open(SERVER, O_WRONLY | O_NONBLOCK);

    if (fd == -1)
        return 0;

    int ok = escrever_tudo(fd, msg, sizeof(Msg));

    close(fd);

    return ok;
}

int esperar_inteiro(int fd, int *valor)
{
    ssize_t n;

    while (1)
    {
        n = read(fd, valor, sizeof(int));

        if (n == sizeof(int))
            return 1;

        if (n == -1)
        {
            if (errno == EINTR)
                continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                usleep(10000);
                continue;
            }

            return 0;
        }

        if (n == 0)
        {
            usleep(10000);
            continue;
        }

        return 0;
    }
}

int construir_comando(int argc, char *argv[], char *destino, size_t tamanho)
{
    destino[0] = '\0';

    for (int i = 3; i < argc; i++)
    {
        size_t usado = strlen(destino);
        size_t arg_len = strlen(argv[i]);

        if (usado + arg_len + 2 > tamanho)
            return 0;

        if (i > 3)
            strcat(destino, " ");

        strcat(destino, argv[i]);
    }

    return destino[0] != '\0';
}

int gerar_command_id()
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    int id = ((int)getpid() * 1000) ^ (int)(tv.tv_usec % 1000);

    if (id < 0)
        id = -id;

    return id;
}

int enviar_done(Msg *msg)
{
    Msg done = *msg;

    done.type = TYPE_DONE;

    return enviar_mensagem(&done);
}

int modo_exec(int argc, char *argv[])
{
    if (argc < 4)
    {
        escrever_formatado(STDERR_FILENO, "Uso: ./runner -e <user-id> \"<command>\"\n");
        return 1;
    }

    char fifo_path[50];

    int fifo_fd = criar_fifo_privado(fifo_path, sizeof(fifo_path));

    if (fifo_fd == -1)
    {
        escrever_formatado(STDERR_FILENO, "[runner] failed to create private fifo\n");
        return 1;
    }

    Msg msg;

    memset(&msg, 0, sizeof(Msg));

    msg.pid = getpid();
    msg.type = TYPE_EXEC;
    msg.user_id = atoi(argv[2]);
    msg.command_id = gerar_command_id();

    if (construir_comando(argc, argv, msg.command, MAX_COMMAND) == 0)
    {
        escrever_formatado(STDERR_FILENO, "[runner] invalid command\n");
        fechar_fifo_privado(fifo_fd, fifo_path);
        return 1;
    }

    if (enviar_mensagem(&msg) == 0)
    {
        escrever_formatado(STDERR_FILENO, "[runner] failed to contact controller\n");
        fechar_fifo_privado(fifo_fd, fifo_path);
        return 1;
    }

    escrever_formatado(STDOUT_FILENO, "[runner] command %d submitted\n", msg.command_id);

    int auth = RESP_REJECTED;

    if (esperar_inteiro(fifo_fd, &auth) == 0)
    {
        escrever_formatado(STDERR_FILENO, "[runner] authorization failed\n");
        fechar_fifo_privado(fifo_fd, fifo_path);
        return 1;
    }

    if (auth != RESP_AUTHORIZED)
    {
        escrever_formatado(STDERR_FILENO, "[runner] command %d rejected\n", msg.command_id);
        fechar_fifo_privado(fifo_fd, fifo_path);
        return 1;
    }

    escrever_formatado(STDOUT_FILENO, "[runner] executing command %d...\n", msg.command_id);

    int status_comando = executar_comando(msg.command);

    escrever_formatado(STDOUT_FILENO, "[runner] command %d finished\n", msg.command_id);

    if (enviar_done(&msg) == 0)
    {
        escrever_formatado(STDERR_FILENO, "[runner] failed to notify controller\n");
        fechar_fifo_privado(fifo_fd, fifo_path);
        return 1;
    }

    fechar_fifo_privado(fifo_fd, fifo_path);

    return status_comando;
}

int modo_status()
{
    char fifo_path[50];

    int fifo_fd = criar_fifo_privado(fifo_path, sizeof(fifo_path));

    if (fifo_fd == -1)
    {
        escrever_formatado(STDERR_FILENO, "[runner] failed to create private fifo\n");
        return 1;
    }

    Msg msg;

    memset(&msg, 0, sizeof(Msg));

    msg.pid = getpid();
    msg.type = TYPE_STATUS;

    if (enviar_mensagem(&msg) == 0)
    {
        escrever_formatado(STDERR_FILENO, "[runner] failed to contact controller\n");
        fechar_fifo_privado(fifo_fd, fifo_path);
        return 1;
    }

    char buffer[512];
    int recebeu_dados = 0;
    int tentativas = 0;

    while (tentativas < MAX_WAIT_ITER)
    {
        ssize_t n = read(fifo_fd, buffer, sizeof(buffer));

        if (n > 0)
        {
            recebeu_dados = 1;
            tentativas = 0;

            if (escrever_tudo(STDOUT_FILENO, buffer, (size_t)n) == 0)
                break;
        }
        else if (n == 0)
        {
            if (recebeu_dados)
                break;

            usleep(10000);
            tentativas++;
        }
        else
        {
            if (errno == EINTR)
                continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                usleep(10000);
                tentativas++;
                continue;
            }

            break;
        }
    }

    fechar_fifo_privado(fifo_fd, fifo_path);

    if (!recebeu_dados)
    {
        escrever_formatado(STDERR_FILENO, "[runner] failed to receive status\n");
        return 1;
    }

    return 0;
}

int modo_stop()
{
    char fifo_path[50];

    int fifo_fd = criar_fifo_privado(fifo_path, sizeof(fifo_path));

    if (fifo_fd == -1)
    {
        escrever_formatado(STDERR_FILENO, "[runner] failed to create private fifo\n");
        return 1;
    }

    Msg msg;

    memset(&msg, 0, sizeof(Msg));

    msg.pid = getpid();
    msg.type = TYPE_STOP;

    if (enviar_mensagem(&msg) == 0)
    {
        escrever_formatado(STDERR_FILENO, "[runner] failed to contact controller\n");
        fechar_fifo_privado(fifo_fd, fifo_path);
        return 1;
    }

    escrever_formatado(STDOUT_FILENO, "[runner] sent shutdown notification\n");
    escrever_formatado(STDOUT_FILENO, "[runner] waiting for controller to shutdown...\n");

    int resposta = RESP_REJECTED;

    if (esperar_inteiro(fifo_fd, &resposta) == 1 && resposta == RESP_SHUTDOWN_DONE)
    {
        escrever_formatado(STDOUT_FILENO, "[runner] controller exited.\n");
    }
    else
    {
        escrever_formatado(STDERR_FILENO, "[runner] failed to receive shutdown confirmation\n");
        fechar_fifo_privado(fifo_fd, fifo_path);
        return 1;
    }

    fechar_fifo_privado(fifo_fd, fifo_path);

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        escrever_formatado(STDERR_FILENO, "Uso: ./runner <-e|-c|-s> [args]\n");
        return 1;
    }

    if (strcmp(argv[1], "-e") == 0)
        return modo_exec(argc, argv);

    if (strcmp(argv[1], "-c") == 0)
        return modo_status();

    if (strcmp(argv[1], "-s") == 0)
        return modo_stop();

    escrever_formatado(STDERR_FILENO, "Uso: ./runner <-e|-c|-s> [args]\n");

    return 1;
}
