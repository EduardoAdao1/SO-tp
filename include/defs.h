#ifndef DEFS_H
#define DEFS_H

#include <sys/time.h>
#include <sys/types.h>

#define SERVER "Fifo_Server"
#define TYPE_EXEC 1
#define TYPE_STATUS 2
#define TYPE_STOP 3
#define TYPE_DONE 4
#define MAX_COMMAND 256

// Estrutura de uma mensagem/pedido
typedef struct
{
    pid_t pid;                 // Pid do runner para o pipe de resposta
    int type;                  // Tipo da mensagem
    int user_id;               // Id do user
    int command_id;            // Id único do comando
    char command[MAX_COMMAND]; // Comando a executar
} Msg;

// Estrutura para guardar os dados dos comandos em execução
typedef struct inicio
{
    pid_t pid;                 // Pid do runner
    int user_id;               // Id do user
    int command_id;            // Id único do comando
    struct timeval t_submit;   // Hora em que o comando foi submetido
    char command[MAX_COMMAND]; // Comando em execução
    struct inicio *next;
} Inicio;

#endif