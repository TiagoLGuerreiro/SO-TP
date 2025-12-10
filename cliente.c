#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include "comum.h" 

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

    // 5. Verificar Login
    if (resp.success) {
        printf("\n=== LOGIN SUCESSO ===\n");
        printf("Msg: %s\n", resp.message);

        // CRIAR PIPE INTERNO (Filho -> Pai)
        int p_interno[2];
        pipe(p_interno);

        // Tornar a leitura do pipe interno NÃO BLOQUEANTE no pai
        int flags = fcntl(p_interno[0], F_GETFL, 0);
        fcntl(p_interno[0], F_SETFL, flags | O_NONBLOCK);

        pid_t pid = fork();

        if (pid == 0) {
            // --- PROCESSO FILHO ---
            close(p_interno[0]); // Fecha leitura

            while(1) {
                int fd_leitura = open(my_pipe, O_RDONLY); 
                ControllerResponse notificacao;

                while(read(fd_leitura, &notificacao, sizeof(ControllerResponse)) > 0) {
                    printf("\n[INFO]: %s\n> ", notificacao.message);
                    fflush(stdout); 

                    // SE FOR MENSAGEM "CHEGUEI", AVISAR O PAI
                    char temp_pipe[MAX_PIPE_NAME];
                    if (sscanf(notificacao.message, "CHEGUEI %s", temp_pipe) == 1) {
                        write(p_interno[1], temp_pipe, MAX_PIPE_NAME);
                    }
                }
                close(fd_leitura);
            }
            exit(0);
        }
        else {
            // --- PROCESSO PAI ---
            close(p_interno[1]); // Fecha escrita
            
            printf("A aguardar comandos (agendar, ler, sair)...\n> ");
            char linha[200]; 
            char ultimo_pipe_veiculo[MAX_PIPE_NAME] = "";

            while(fgets(linha, 200, stdin) != NULL) {
                linha[strcspn(linha, "\n")] = 0;

                // Verificar se o filho mandou algum pipe
                char pipe_do_filho[MAX_PIPE_NAME];
                if (read(p_interno[0], pipe_do_filho, MAX_PIPE_NAME) > 0) {
                    strcpy(ultimo_pipe_veiculo, pipe_do_filho);
                }

                if(strcmp(linha, "terminar") == 0) {
                    ClientRequest req_out;
                    req_out.command_type = CMD_SAIR;
                    req_out.pid = getpid();
                    int fd = open(FIFO_CONTROLLER_REQUESTS, O_WRONLY);
                    write(fd, &req_out, sizeof(ClientRequest));
                    close(fd);
                    break;
                }
                else if (strncmp(linha, "agendar", 7) == 0) {
                    ClientRequest req_ag;
                    req_ag.command_type = CMD_AGENDAR;
                    req_ag.pid = getpid();
                    strcpy(req_ag.username, username);
                    strcpy(req_ag.response_pipe_name, my_pipe);
                    strcpy(req_ag.data, linha + 8); 
                    
                    int fd = open(FIFO_CONTROLLER_REQUESTS, O_WRONLY);
                    write(fd, &req_ag, sizeof(ClientRequest));
                    close(fd);
                }
                else if (strncmp(linha, "entrar", 6) == 0) {
                    char pipe_v[MAX_PIPE_NAME], dest[50];
                    
                    if (sscanf(linha, "entrar %s %s", pipe_v, dest) == 2) {
                        strcpy(ultimo_pipe_veiculo, pipe_v);
                    } 
                    else if (sscanf(linha, "entrar %s", dest) == 1) {
                        // Verifica novamente o pipe interno antes de falhar
                        if (read(p_interno[0], pipe_do_filho, MAX_PIPE_NAME) > 0) {
                            strcpy(ultimo_pipe_veiculo, pipe_do_filho);
                        }

                        if (strlen(ultimo_pipe_veiculo) == 0) {
                            printf("Erro: Ainda não sei qual é o veículo. Espera pela msg CHEGUEI ou usa: entrar <pipe> <destino>\n");
                        } else {
                            strcpy(pipe_v, ultimo_pipe_veiculo);
                            sprintf(linha, "entrar %s %s", pipe_v, dest);
                        }
                    }

                    if (strlen(ultimo_pipe_veiculo) > 0) {
                        int fd_v = open(pipe_v, O_WRONLY);
                        if (fd_v != -1) {
                            write(fd_v, linha, strlen(linha)+1);
                            close(fd_v);
                            printf("A entrar no veículo (%s)...\n", pipe_v);
                        } else {
                            printf("Erro: Não consegui contactar o veículo %s.\n", pipe_v);
                        }
                    }
                }
                else if (strcmp(linha, "sair") == 0) {
                    if (strlen(ultimo_pipe_veiculo) > 0) {
                        int fd_v = open(ultimo_pipe_veiculo, O_WRONLY);
                        if (fd_v != -1) {
                            write(fd_v, "sair", 5);
                            close(fd_v);
                            printf("Pedido de paragem enviado ao veículo.\n");
                            strcpy(ultimo_pipe_veiculo, "");
                        } else {
                            printf("Erro: Veículo não responde.\n");
                        }
                    } else {
                        printf("Erro: Não estás num veículo.\n");
                    }
                }
                else if (strcmp(linha, "consultar") == 0) {
                    ClientRequest req_c;
                    req_c.command_type = CMD_CONSULTAR;
                    req_c.pid = getpid();
                    strcpy(req_c.username, username);
                    strcpy(req_c.response_pipe_name, my_pipe);
                    
                    int fd = open(FIFO_CONTROLLER_REQUESTS, O_WRONLY);
                    write(fd, &req_c, sizeof(ClientRequest));
                    close(fd);
                }
                else if (strncmp(linha, "cancelar", 8) == 0) {
                     int id_c;
                     if(sscanf(linha, "cancelar %d", &id_c) == 1) {
                        ClientRequest req_c;
                        req_c.command_type = CMD_CANCELAR_REQ;
                        req_c.pid = getpid();
                        strcpy(req_c.username, username);
                        strcpy(req_c.response_pipe_name, my_pipe);
                        sprintf(req_c.data, "%d", id_c); 
                        
                        int fd = open(FIFO_CONTROLLER_REQUESTS, O_WRONLY);
                        write(fd, &req_c, sizeof(ClientRequest));
                        close(fd);
                     } else {
                         printf("Uso: cancelar <id>\n");
                     }
                }

                printf("> ");
            }
            
            kill(pid, SIGKILL);
            unlink(my_pipe);
        } // Fim do else (Pai)
    } else { // Fim do if(success) e inicio do else
        printf("\n=== LOGIN ERRO ===\n");
        printf("Motivo: %s\n", resp.message);
        unlink(my_pipe);
    }

    return 0;
}