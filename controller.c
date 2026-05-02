#include <stdio.h>
#include <stdlib.h> 
#include <string.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h> 
#include <defs.h>
#include <queue.h>

Inicio *em_exec = NULL;
Node *queue = NULL;
int max_comandos = 1;
int comandos_ativos = 0;

// Função que da autorização ao runner para correr o comando
void autorizar(pid_t pid){
    // Abrimos o FIFO privado do runner
    char fifo_resposta[50];
    sprintf(fifo_resposta,"fifo_%d",pid);
    int resposta_fd = open(fifo_resposta,O_WRONLY);
    if(resposta_fd != -1){
        int auth = 1;
        write(resposta_fd,&auth,sizeof(int));
        close(resposta_fd);
    }
}

// Registra o momento em que uma mensagem/pedido entra em execução
void registar_exec(Msg *msg, struct timeval t){
    Inicio *n = malloc(sizeof(Inicio));
    n->pid = msg->pid;
    n->t_submit = t;
    n->next = em_exec;
    strcpy(n->command,msg->command);
    em_exec = n;
}

// Remove um pedido da fila em_exec quando esta acaba, 
// e guardamos na memória da mensagem o comando para conseguir escrever no log qual o comando terminado
struct timeval remove_exec(Msg *msg){
    Inicio *curr = em_exec, *prev = NULL;
    struct timeval t = {0};
    while(curr){
        if(curr->pid == msg->pid){
            t = curr->t_submit;
            if(prev) prev->next = curr->next;
            else em_exec = curr->next;
            strcpy(msg->command,curr->command);
            free(curr);
            return t;
        }
        prev = curr;
        curr = curr->next;
    }
    return t;
}

// Função que escreve o log dos comandos executados e calcula o seu tempo de execução
void escrever_log(Msg m,struct timeval t){
    struct timeval fim;
    gettimeofday(&fim,NULL);
    // Converte o tempo de execução para milissegundos
    long ms = (fim.tv_sec - t.tv_sec) * 1000 + (fim.tv_usec - t.tv_usec) / 1000;
    // Abre o ficheiro(criando se nao houver ainda) e escreve a informação
    int fd = open("log.txt", O_WRONLY | O_APPEND | O_CREAT, 0666);
    char buffer[256];
    int bytes_written = sprintf(buffer,"User- %d | Cmd- %s | Duração- %ldms \n", m.user_id,m.command,ms);
    write(fd,buffer,bytes_written);
    close(fd);
}

// Função que envia o status dos pedidos quando pedido pelo cliente
void enviar_status(pid_t cliente_pid) {
    char fifo_path[50];
    sprintf(fifo_path, "fifo_%d", cliente_pid);
    int fd = open(fifo_path, O_WRONLY);
    if (fd == -1) return;

    char buffer[1024];
    int n;

    // Escreve Comandos em Execução
    n = sprintf(buffer, "Em execução \n");
    write(fd, buffer, n);
    Inicio *curr = em_exec;
    while(curr) {
        n = sprintf(buffer, "PID: %d | Comando: %s \n", curr->pid,curr->command);
        write(fd, buffer, n);
        curr = curr->next;
    }

    // Escreve Comandos em Espera
    n = sprintf(buffer, "Em Espera \n");
    write(fd, buffer, n);
    Node *espera = queue;
    while(espera) {
        n = sprintf(buffer, "PID: %d | Cmd: %s\n", espera->pedido.pid, espera->pedido.command);
        write(fd, buffer, n);
        espera = espera->next;
    }

    close(fd);
}

int main(int argc,char* argv[]){
    // recebe o valor de maximo comandos a executar ao mesmo tempo do utilizador
    if(argc > 1) max_comandos = atoi(argv[1]);

    int bytes_read;
    
    // Criar o fifo do servidor
    mkfifo(SERVER,0666);

    int server_fd = open(SERVER,O_RDWR);

    Msg msg;
    int running = 1;
    while(running == 1 && (bytes_read = read(server_fd,&msg,sizeof(Msg))) > 0){
        
        struct timeval agora;
        gettimeofday(&agora,NULL);

        if(msg.type == TYPE_EXEC) {
            // Se ainda houver espaço para executar o comando damos autorização ao runner 
            if(comandos_ativos < max_comandos){
                comandos_ativos++;
                registar_exec(&msg,agora);
                autorizar(msg.pid);
            }
            else {
                // Se não houver vaga adicionamos à queue
                enqueue(&queue,msg,agora);
            }
        }
        // Se o tipo for de DONE damos log do tempo de execução,removemos da lista de mensagens em exec e vemos se há um proximo comando a executar
        else if(msg.type == TYPE_DONE){
            struct timeval t = remove_exec(&msg);
            escrever_log(msg,t);
            comandos_ativos--;

            if(queue != NULL && comandos_ativos < max_comandos){
                struct timeval tempo;
                Msg prox = dequeue(&queue,&tempo);
                comandos_ativos++;
                registar_exec(&prox,tempo);
                autorizar(prox.pid);
            }
        }
        // Se o tipo for pedir o status dos pedidos chamamos a função que faz isso
        else if(msg.type == TYPE_STATUS){
            enviar_status(msg.pid);
        }
        // Se o pedido for para encerrar o controlador, trocamos running = 0 e no proximo ciclo do while o controlador deixa de correr
        else if(msg.type == TYPE_STOP){
            printf("A encerrar controlador \n");
            running = 0;
        }
    }
    close(server_fd);
    unlink(SERVER);
    printf("Controlador encerrado \n");
    return 0;
}