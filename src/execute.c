#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "defs.h"
#include "execute.h"

#define MAX_ARGS 64
#define MAX_TOKENS 128
#define MAX_COMMANDS 32

typedef struct comando_exec
{
    char *argv[MAX_ARGS];
    char *input_file;
    char *output_file;
    char *error_file;
} ComandoExec;

static int escrever_tudo(int fd, const void *buffer, size_t tamanho)
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

static int escrever_formatado(int fd, const char *formato, ...)
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

static int eh_espaco(char c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

static int eh_operador(const char *token)
{
    return strcmp(token, "|") == 0 ||
           strcmp(token, "<") == 0 ||
           strcmp(token, ">") == 0 ||
           strcmp(token, "2>") == 0;
}

static int guardar_token(char tokens_storage[MAX_TOKENS][MAX_COMMAND], char *tokens[], int *ntokens, const char *inicio, size_t tamanho)
{
    if (*ntokens >= MAX_TOKENS - 1)
        return 0;

    if (tamanho >= MAX_COMMAND)
        return 0;

    memcpy(tokens_storage[*ntokens], inicio, tamanho);
    tokens_storage[*ntokens][tamanho] = '\0';

    tokens[*ntokens] = tokens_storage[*ntokens];
    (*ntokens)++;

    return 1;
}

static int tokenizar_comando(const char *command, char *tokens[], char tokens_storage[MAX_TOKENS][MAX_COMMAND])
{
    int ntokens = 0;
    const char *p = command;

    while (*p != '\0')
    {
        while (*p != '\0' && eh_espaco(*p))
            p++;

        if (*p == '\0')
            break;

        if (*p == '2' && *(p + 1) == '>')
        {
            if (guardar_token(tokens_storage, tokens, &ntokens, "2>", 2) == 0)
                return -1;

            p += 2;
            continue;
        }

        if (*p == '|' || *p == '<' || *p == '>')
        {
            if (guardar_token(tokens_storage, tokens, &ntokens, p, 1) == 0)
                return -1;

            p++;
            continue;
        }

        if (*p == '"' || *p == '\'')
        {
            char quote = *p;
            const char *inicio;

            p++;
            inicio = p;

            while (*p != '\0' && *p != quote)
                p++;

            if (*p != quote)
                return -1;

            if (guardar_token(tokens_storage, tokens, &ntokens, inicio, p - inicio) == 0)
                return -1;

            p++;
            continue;
        }

        const char *inicio = p;

        while (*p != '\0' &&
               !eh_espaco(*p) &&
               *p != '|' &&
               *p != '<' &&
               *p != '>' &&
               !(*p == '2' && *(p + 1) == '>'))
        {
            p++;
        }

        if (p == inicio)
            return -1;

        if (guardar_token(tokens_storage, tokens, &ntokens, inicio, p - inicio) == 0)
            return -1;
    }

    tokens[ntokens] = NULL;

    return ntokens;
}

static void inicializar_comando(ComandoExec *cmd)
{
    for (int i = 0; i < MAX_ARGS; i++)
        cmd->argv[i] = NULL;

    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->error_file = NULL;
}

static int preparar_comandos(char *tokens[], int ntokens, ComandoExec comandos[], int *num_comandos)
{
    int c = 0;
    int a = 0;

    inicializar_comando(&comandos[c]);

    for (int i = 0; i < ntokens; i++)
    {
        if (strcmp(tokens[i], "|") == 0)
        {
            // Evita casos como "| wc".
            if (a == 0)
                return 0;

            comandos[c].argv[a] = NULL;

            c++;

            if (c >= MAX_COMMANDS)
                return 0;

            inicializar_comando(&comandos[c]);
            a = 0;
        }
        else if (strcmp(tokens[i], "<") == 0)
        {
            if (i + 1 >= ntokens || eh_operador(tokens[i + 1]))
                return 0;

            comandos[c].input_file = tokens[i + 1];
            i++;
        }
        else if (strcmp(tokens[i], ">") == 0)
        {
            if (i + 1 >= ntokens || eh_operador(tokens[i + 1]))
                return 0;

            comandos[c].output_file = tokens[i + 1];
            i++;
        }
        else if (strcmp(tokens[i], "2>") == 0)
        {
            if (i + 1 >= ntokens || eh_operador(tokens[i + 1]))
                return 0;

            comandos[c].error_file = tokens[i + 1];
            i++;
        }
        else
        {
            if (a >= MAX_ARGS - 1)
                return 0;

            comandos[c].argv[a] = tokens[i];
            a++;
        }
    }

    // Evita casos como "cat file |".
    if (a == 0)
        return 0;

    comandos[c].argv[a] = NULL;
    *num_comandos = c + 1;

    return 1;
}

static int aplicar_redirecionamentos(ComandoExec *cmd)
{
    if (cmd->input_file != NULL)
    {
        int fd = open(cmd->input_file, O_RDONLY);

        if (fd == -1)
            return 0;

        if (dup2(fd, STDIN_FILENO) == -1)
        {
            close(fd);
            return 0;
        }

        close(fd);
    }

    if (cmd->output_file != NULL)
    {
        int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);

        if (fd == -1)
            return 0;

        if (dup2(fd, STDOUT_FILENO) == -1)
        {
            close(fd);
            return 0;
        }

        close(fd);
    }

    if (cmd->error_file != NULL)
    {
        int fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);

        if (fd == -1)
            return 0;

        if (dup2(fd, STDERR_FILENO) == -1)
        {
            close(fd);
            return 0;
        }

        close(fd);
    }

    return 1;
}

static void fechar_pipes(int pipes[][2], int n_pipes)
{
    for (int i = 0; i < n_pipes; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static void esperar_filhos(pid_t filhos[], int n_filhos)
{
    int status;

    for (int i = 0; i < n_filhos; i++)
    {
        while (waitpid(filhos[i], &status, 0) == -1)
        {
            if (errno == EINTR)
                continue;

            break;
        }
    }
}

static int executar_pipeline(ComandoExec comandos[], int num_comandos)
{
    int pipes[MAX_COMMANDS][2];
    pid_t filhos[MAX_COMMANDS];
    int n_pipes = num_comandos - 1;
    int pipes_criados = 0;
    int filhos_criados = 0;

    for (int i = 0; i < n_pipes; i++)
    {
        if (pipe(pipes[i]) == -1)
        {
            fechar_pipes(pipes, pipes_criados);
            return 1;
        }

        pipes_criados++;
    }

    for (int i = 0; i < num_comandos; i++)
    {
        filhos[i] = fork();

        if (filhos[i] == -1)
        {
            fechar_pipes(pipes, pipes_criados);
            esperar_filhos(filhos, filhos_criados);
            return 1;
        }

        if (filhos[i] == 0)
        {
            if (i > 0)
            {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1)
                    _exit(1);
            }

            if (i < num_comandos - 1)
            {
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1)
                    _exit(1);
            }

            fechar_pipes(pipes, pipes_criados);

            // Os redirecionamentos finais têm prioridade sobre os descritores da pipeline.
            if (aplicar_redirecionamentos(&comandos[i]) == 0)
            {
                escrever_formatado(STDERR_FILENO, "[runner] failed to redirect command\n");
                _exit(1);
            }

            execvp(comandos[i].argv[0], comandos[i].argv);

            escrever_formatado(STDERR_FILENO, "[runner] failed to execute command\n");
            _exit(127);
        }

        filhos_criados++;
    }

    fechar_pipes(pipes, pipes_criados);

    int status;
    int estado_final = 0;

    for (int i = 0; i < num_comandos; i++)
    {
        while (waitpid(filhos[i], &status, 0) == -1)
        {
            if (errno == EINTR)
                continue;

            estado_final = 1;
            break;
        }

        // O resultado da pipeline é o estado do último comando.
        if (i == num_comandos - 1)
        {
            if (WIFEXITED(status))
                estado_final = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                estado_final = 128 + WTERMSIG(status);
            else
                estado_final = 1;
        }
    }

    return estado_final;
}

int executar_comando(char *command)
{
    char *tokens[MAX_TOKENS];
    char tokens_storage[MAX_TOKENS][MAX_COMMAND];
    ComandoExec comandos[MAX_COMMANDS];
    int num_comandos = 0;

    int ntokens = tokenizar_comando(command, tokens, tokens_storage);

    if (ntokens <= 0)
    {
        escrever_formatado(STDERR_FILENO, "[runner] invalid command\n");
        return 1;
    }

    if (preparar_comandos(tokens, ntokens, comandos, &num_comandos) == 0)
    {
        escrever_formatado(STDERR_FILENO, "[runner] invalid command\n");
        return 1;
    }

    return executar_pipeline(comandos, num_comandos);
}
