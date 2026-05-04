#ifndef DEFS_H
#define DEFS_H

#include <sys/time.h>
#include <sys/types.h>

#define SERVER "Fifo_Server"

#define TYPE_EXEC 1
#define TYPE_STATUS 2
#define TYPE_STOP 3
#define TYPE_DONE 4

#define RESP_REJECTED 0
#define RESP_AUTHORIZED 1
#define RESP_SHUTDOWN_DONE 2

#define MAX_COMMAND 256

typedef struct
{
    pid_t pid;
    int type;
    int user_id;
    int command_id;
    char command[MAX_COMMAND];
} Msg;

typedef struct inicio
{
    pid_t pid;
    int user_id;
    int command_id;
    struct timeval t_submit;
    char command[MAX_COMMAND];
    struct inicio *next;
} Inicio;

#endif
