#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
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

    // 5. Verificar Login
    if (resp.success) {
        printf("\n=== LOGIN SUCESSO ===\n");
        printf("Msg: %s\n", resp.message);

        // --- INÍCIO DA ALTERAÇÃO ---

        // NÃO feche nem apague o pipe aqui! Precisamos dele para o futuro.
        // close(fd_resp);  <-- REMOVER
        // unlink(my_pipe); <-- REMOVER (Mover para o fim)

        pid_t pid = fork();

        if (pid == 0) {
            // PROCESSO FILHO: Escuta o Controlador/Veículo
            // Reabre o pipe para leitura contínua (bloqueante)
            // Nota: O open anterior consumiu a msg de login, este open espera novas
            int fd_leitura = open(my_pipe, O_RDONLY); 
            ControllerResponse notificacao;

            while(read(fd_leitura, &notificacao, sizeof(ControllerResponse)) > 0) {
                printf("\n[INFO]: %s\n> ", notificacao.message);
                fflush(stdout); // Forçar a escrita no ecrã
            }
            close(fd_leitura);
            exit(0);
        } 
        else {
            // PROCESSO PAI: Lê do utilizador
            printf("A aguardar comandos (agendar, ler, sair)...\n> ");
            char linha[100];
            
            // Usar fgets é melhor que scanf para ler linhas inteiras (com espaços)
            while(fgets(linha, 100, stdin) != NULL) {
                // Remover o \n do final
                linha[strcspn(linha, "\n")] = 0;

                if(strcmp(linha, "sair") == 0) {
                    // Enviar pedido de saída
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
                    strcpy(req_ag.response_pipe_name, my_pipe); // <--- ADICIONA ISTO
                    strcpy(req_ag.data, linha + 8); 
                    
                    int fd = open(FIFO_CONTROLLER_REQUESTS, O_WRONLY);
                    write(fd, &req_ag, sizeof(ClientRequest));
                    close(fd);
                }
                else if (strncmp(linha, "entrar", 6) == 0) {
                    // O utilizador escreveu "entrar Lisboa"
                    // Precisamos de saber QUAL é o pipe do veículo.
                    // Simplificação: O utilizador tem de escrever o pipe que apareceu no ecrã?
                    // Ou o comando é "entrar <pipe> <destino>"?
                    
                    // Como ainda não guardamos o estado no cliente, vamos fazer um truque para testar:
                    // O utilizador vai escrever: entrar <pipe_do_veiculo> <destino>
                    // Exemplo: entrar /tmp/veiculo_12345 Lisboa
                    
                    char pipe_veiculo[MAX_PIPE_NAME];
                    char destino[50];
                    
                    if (sscanf(linha, "entrar %s %s", pipe_veiculo, destino) == 2) {
                        int fd_v = open(pipe_veiculo, O_WRONLY);
                        if (fd_v != -1) {
                            // Enviar apenas a string "entrar Lisboa" ou só "Lisboa"
                            write(fd_v, linha, strlen(linha)+1);
                            close(fd_v);
                            printf("Comando enviado para o veículo!\n");
                        } else {
                            printf("Erro: Não consegui abrir o pipe do veículo.\n");
                        }
                    } else {
                        printf("Uso: entrar <pipe_veiculo> <destino>\n");
                    }
                }
                printf("> ");
            }
            
            // Matar o filho e limpar o FIFO ao sair
            kill(pid, SIGKILL);
            unlink(my_pipe);
        }
        // --- FIM DA ALTERAÇÃO ---

    } else {
        printf("\n=== LOGIN ERRO ===\n");
        printf("Motivo: %s\n", resp.message);
        unlink(my_pipe); // Aqui sim, podemos apagar se falhou
    }

    return 0;
}