#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h> // usa se na funçao waitpid

#define VEICULO_EXEC "./veiculo"
#define MAX_TELEMETRIA_MSG 256

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

int main(int argc, char *argv[]) {
    printf("--- INÍCIO DA SIMULAÇÃO (Controlador) ---\n");
    veiculo_start_ler_telemetria(); // lançar a função de simulação

    return 0;
}