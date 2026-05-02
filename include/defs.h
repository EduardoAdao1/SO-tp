#include <sys/time.h>
#include <fcntl.h>
#include <sys/types.h>

#define SERVER "Fifo_Ferver"
#define CLIENT "Fifo_Client"
#define TYPE_EXEC 1
#define TYPE_STATUS 2
#define TYPE_STOP 3
#define TYPE_DONE 4



// Estruta de uma mensagem/pedido
typedef struct {
    pid_t pid;  //Pid do utilizadar para o pipe de resposta
    int type;   // tipo da mensagem
    uid_t user_id;  // id do user
    char command[256]; //Comando a executar
} Msg;

//Estrutura para guardar os tempos de inicio de execução de mensagens
typedef struct inicio {
    pid_t pid; // pid do runner
    struct timeval t_submit; //hora em que o comando entrou em execução
    char command[256]; // Comando em execução
    struct inicio *next;
} Inicio;

