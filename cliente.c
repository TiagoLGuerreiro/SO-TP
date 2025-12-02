#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "comum.h" // Importante!

#define FIFO_CONTROLLER_REQUESTS "/tmp/SO_TAXI_PEDIDOS"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: ./cliente <username>\n");
        return 1;
    }
    char *username = argv[1];

    // 1. Criar Pipe de Resposta Único
    char my_pipe[MAX_PIPE_NAME];
    sprintf(my_pipe, "/tmp/resp_%d", getpid());
    mkfifo(my_pipe, 0666);

    // 2. Preparar Pedido de Login
    ClientRequest req;
    req.command_type = CMD_LOGIN;
    req.pid = getpid();
    strcpy(req.username, username);
    strcpy(req.response_pipe_name, my_pipe);

    // 3. Enviar Pedido
    int fd_req = open(FIFO_CONTROLLER_REQUESTS, O_WRONLY);
    if(fd_req == -1) {
        printf("Erro: Controlador nao esta a correr.\n");
        unlink(my_pipe);
        return 1;
    }
    write(fd_req, &req, sizeof(ClientRequest));
    close(fd_req);

    // 4. Aguardar Resposta
    int fd_resp = open(my_pipe, O_RDONLY);
    ControllerResponse resp;
    read(fd_resp, &resp, sizeof(ControllerResponse));
    close(fd_resp);
    unlink(my_pipe); // Apaga o pipe depois de usar

    // 5. Verificar Login
    if (resp.success) {
        printf("\n=== LOGIN SUCESSO ===\n");
        printf("Msg: %s\n", resp.message);
        printf("A aguardar comandos (digite 'terminar' para terminar)...\n");
        
        // Loop simples para não terminar logo
        char cmd[100];
        while(1) {
        printf("> ");
        scanf("%s", cmd);
        if(strcmp(cmd, "terminar")==0) {
            // AVISAR O CONTROLADOR QUE VOU SAIR
            ClientRequest req_out;
            req_out.command_type = CMD_SAIR;
            req_out.pid = getpid();
            // Não precisamos de preencher username ou pipe para sair, o PID chega

            int fd_req = open(FIFO_CONTROLLER_REQUESTS, O_WRONLY);
            if(fd_req != -1) {
                write(fd_req, &req_out, sizeof(ClientRequest));
                close(fd_req);
            }
            break; // Sai do loop e termina
        }
    }

    } else {
        printf("\n=== LOGIN ERRO ===\n");
        printf("O servidor recusou a entrada.\n");
        printf("Motivo: %s\n", resp.message);
    }

    return 0;
}