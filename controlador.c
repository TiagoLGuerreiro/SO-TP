#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h> // usa se na funçao waitpid
#include <sys/types.h> // para mkfifo()
#include <sys/stat.h>  // para mkfifo()
#include <fcntl.h>     // para open()
#include <string.h>

#define VEICULO_EXEC "./veiculo"
#define MAX_TELEMETRIA_MSG 256
#define FIFO_CONTROLLER_REQUESTS "/tmp/SO_TAXI_PEDIDOS"
#define MAX_COMMAND_SIZE 1024 // tamanho maximo de uma msg
// No topo do controlador.c, adicione:
#include "comum.h" 

// E defina isto (o nome tem que ser igual ao do cliente)
#define FIFO_CONTROLLER_REQUESTS "/tmp/SO_TAXI_PEDIDOS"

void veiculo_start_ler_telemetria(){
    int pipe_fd[2]; //0 - leitura (controlador) 1 - escrita (veiculo)

    if(pipe(pipe_fd) == -1){ //criar pipe anonimo
        perror("Erro ao criar pipe");
        return;
    }

    pid_t pid = fork(); //duplicar o processo

    if(pid == -1){
        perror("Erro ao fazer fork");
        return;
    } else if(pid == 0){ //logica do filho (veiculo)
        close(pipe_fd[0]); //o filho apenas precisa de escrever, nao de ler

        if(dup2(pipe_fd[1], STDOUT_FILENO) == -1){ //redireciona o stdout (File Descriptor 1) para a extremidade de escrita do pipe
            perror("Erro ao redirecionar stdout (dup2)");
            exit(EXIT_FAILURE);
        }

        close(pipe_fd[1]); //ja nao é preciso

        execl(VEICULO_EXEC, VEICULO_EXEC, "1", "30", "tmp/demo_pipe_cliente", (char *)NULL); //executa o programa veiculo

        perror("Erro ao executar veiculo"); //so acontece em caso de erro anteriormente
        exit(EXIT_FAILURE);
    } else { //processo pai (controlador)
        close(pipe_fd[1]); // pai apenas precisa de ler

        char buffer[MAX_TELEMETRIA_MSG];
        ssize_t bytes_read;
        
        printf("[CONTROLADOR] Lancei veiculo %d. A escutar a telemetria...", pid);
    
        //loop para ler o que o veiculo envia, para quando o veiculo terminar e fechar o pipe
        while((bytes_read = read(pipe_fd[0], buffer, MAX_TELEMETRIA_MSG -1)) > 0){
            buffer[bytes_read] = '\0';
            printf("[TELEMETRIA DO VEICULO %d]: %s", pid, buffer);
        }

        // fecha o pipe de leitura e espera pelo fim do Veículo
        close(pipe_fd[0]);
        waitpid(pid, NULL, 0); 
        printf("[CONTROLADOR] Veículo terminou e foi recolhido.\n");
    }
}

void cliente_com(){
    // tenta remover o FIFO se já existir (para garantir um início limpo)
    unlink(FIFO_CONTROLLER_REQUESTS);

    // mkfifo cria o Named pipe, vai dar permissoes de leitura/escrita ao utilizador (0666)
    if(mkfifo(FIFO_CONTROLLER_REQUESTS, 0666) == -1){
        perror("[CONTROLADOR] Erro ao criar FIFO");
        exit(EXIT_FAILURE);
    }

    printf("[CONTROLADOR] FIFO de Pedidos ('%s') criado com sucesso.\n", FIFO_CONTROLLER_REQUESTS);

    // Abre o FIFO em modo de leitura, o Controlador para aqui até que o primeiro Cliente abra o pipe para escrever.
    int request_fd = open(FIFO_CONTROLLER_REQUESTS, O_RDONLY);
    if (request_fd == -1) {
        perror("[CONTROLADOR] Erro ao abrir FIFO para leitura");
        exit(EXIT_FAILURE);
    }
    printf("[CONTROLADOR] FIFO aberto. À espera do primeiro cliente...\n");

    // Preparamos o buffer para a ESTRUTURA COMPLETA
    ClientRequest request; 
    ssize_t bytes_read;

    while(1){
        // AGORA LÊ A ESTRUTURA COMPLETA (sizeof(ClientRequest))
        bytes_read = read(request_fd, &request, sizeof(ClientRequest));

        if(bytes_read == sizeof(ClientRequest)){
            printf("\n[CONTROLADOR] PEDIDO RECEBIDO:\n");
            printf("  -> Tipo: %d (LOGIN)\n", request.command_type);
            printf("  -> Username: %s\n", request.username);
            printf("  -> PID do Cliente: %d\n", request.pid);
            printf("  -> Pipe Resposta: %s\n", request.response_pipe_name);
            
            // ------------------------------------------------------------------
            // AQUI ESTÁ A LÓGICA DE RESPOSTA (BIDIRECIONALIDADE)
            // ------------------------------------------------------------------
            
            // 1. Preparar a mensagem de resposta
            ControllerResponse response;
            response.success = 1; // Supondo que o login é sempre OK por agora
            strncpy(response.message, "Login aceite com sucesso.", MAX_MESSAGE - 1);
            response.message[MAX_MESSAGE - 1] = '\0';
            
            // 2. Abrir o FIFO de resposta do Cliente em modo de ESCRITA
            int response_fd = open(request.response_pipe_name, O_WRONLY);
            if (response_fd == -1) {
                // Se der erro, o cliente pode ter fechado ou falhado a criar o pipe
                perror("[CONTROLADOR] ERRO ao abrir FIFO de resposta do Cliente");
            } else {
                // 3. Enviar a estrutura de resposta e fechar
                if (write(response_fd, &response, sizeof(ControllerResponse)) == -1) {
                    perror("[CONTROLADOR] ERRO ao escrever no FIFO de resposta");
                }
                close(response_fd);
                printf("[CONTROLADOR] Resposta de Login enviada para %s.\n", request.username);
            }
            
            // Continua a ler o FIFO de pedidos
            
        } else if(bytes_read > 0 && bytes_read < sizeof(ClientRequest)) {
            // Se leu um número de bytes diferente do tamanho da struct, é um erro ou lixo
            printf("[CONTROLADOR] Aviso: Leu dados incompletos ou corrompidos (%zd bytes).\n", bytes_read);

        } else if(bytes_read == 0){
            // Se o cliente fechar o pipe o controlador reabre o
            printf("[CONTROLADOR] Cliente desconectou-se do FIFO. Reabrindo...\n");
            close(request_fd);

            request_fd = open(FIFO_CONTROLLER_REQUESTS, O_RDONLY);
            if(request_fd == -1){
                perror("[CONTROLADOR] Erro ao abrir o FIFO");
                exit(EXIT_FAILURE);
            }
            printf("[CONTROLADOR] FIFO reaberto. À espera de novos pedidos...\n");
        } else {
            perror("[CONTROLADOR] Erro durante a leitura do FIFO");
            break; 
        }
    }

    close(request_fd);
    unlink(FIFO_CONTROLLER_REQUESTS);
}

int main(int argc, char *argv[]) {
    //printf("--- INÍCIO DA SIMULAÇÃO (Controlador) ---\n");
    //veiculo_start_ler_telemetria(); // lançar a função de simulação

    cliente_com();

    return 0;
}