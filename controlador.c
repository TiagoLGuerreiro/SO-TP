#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h> // usa se na funçao waitpid
#include <sys/types.h> // para mkfifo()
#include <sys/stat.h>  // para mkfifo()
#include <fcntl.h>     // para open()

#define VEICULO_EXEC "./veiculo"
#define MAX_TELEMETRIA_MSG 256
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

    //mkfifo cria o Named pipe, vai dar permissoes de leitura/escrita ao utilizador (0666)
    if(mkfifo(FIFO_CONTROLLER_REQUESTS, 0666) == -1){
        perror("[CONTROLADOR] Erro ao criar FIFO");
        exit(EXIT_FAILURE);
    }

    printf("[CONTROLADOR] FIFO de Pedidos ('%s') criado com sucesso.\n", FIFO_CONTROLLER_REQUESTS);

    // abre o FIFO em modo de leitura, o Controlador para aqui até que o primeiro Cliente abra o pipe para escrever.
    int request_fd = open(FIFO_CONTROLLER_REQUESTS, O_RDONLY);
    if (request_fd == -1) {
        perror("[CONTROLADOR] Erro ao abrir FIFO para leitura");
        exit(EXIT_FAILURE);
    }
    printf("[CONTROLADOR] FIFO aberto. À espera do primeiro cliente...\n");
}

int main(int argc, char *argv[]) {
    //printf("--- INÍCIO DA SIMULAÇÃO (Controlador) ---\n");
    //veiculo_start_ler_telemetria(); // lançar a função de simulação

    cliente_com();

    return 0;
}