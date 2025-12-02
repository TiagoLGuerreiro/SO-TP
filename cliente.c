#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "comum.h" // Inclui o novo ficheiro com as structs

// O nome do FIFO global do Controlador (tem que ser o mesmo no controlador.c)
#define FIFO_CONTROLLER_REQUESTS "/tmp/SO_TAXI_PEDIDOS" 

// NOTE: A função 'enviar_comando' desaparece e a lógica vai para o main.

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: ./cliente <username>\n");
        return 1;
    }
    char *username = argv[1];
    
    // 1. Definir o nome do FIFO de resposta pessoal (único)
    char client_response_fifo[MAX_PIPE_NAME];
    snprintf(client_response_fifo, MAX_PIPE_NAME, "/tmp/SO_TAXI_RESP_%s", username);

    // 2. Criar o FIFO de resposta pessoal
    unlink(client_response_fifo); // Limpeza prévia (boa prática)
    if (mkfifo(client_response_fifo, 0666) == -1) {
        perror("[CLIENTE] Erro ao criar FIFO de resposta pessoal");
        return 1;
    }
    printf("[CLIENTE %s] FIFO de Resposta ('%s') criado.\n", username, client_response_fifo);

    // 3. Montar a estrutura de pedido (LOGIN)
    ClientRequest login_request;
    login_request.pid = getpid();
    login_request.command_type = CMD_LOGIN;
    strncpy(login_request.username, username, MAX_USERNAME - 1);
    login_request.username[MAX_USERNAME - 1] = '\0';
    strncpy(login_request.response_pipe_name, client_response_fifo, MAX_PIPE_NAME - 1);
    login_request.response_pipe_name[MAX_PIPE_NAME - 1] = '\0';
    
    // 4. Abrir o FIFO de pedidos do Controlador e enviar a estrutura
    int request_fd = open(FIFO_CONTROLLER_REQUESTS, O_WRONLY);
    if (request_fd == -1) {
        fprintf(stderr, "[CLIENTE %s] ERRO: Controlador não está em funcionamento.\n", username);
        unlink(client_response_fifo);
        return 1; 
    }

    // Enviar a ESTRUTURA completa (tamanho de memória da struct)
    if (write(request_fd, &login_request, sizeof(ClientRequest)) == -1) {
        perror("[CLIENTE] Erro ao enviar pedido de login");
        close(request_fd);
        unlink(client_response_fifo);
        return 1;
    }
    close(request_fd);
    printf("[CLIENTE %s] Pedido de login enviado. À espera de resposta...\n", username);

    // 5. Abrir o FIFO de resposta pessoal (agora para leitura) e esperar pela resposta
    int response_fd = open(client_response_fifo, O_RDONLY);
    if (response_fd == -1) {
        perror("[CLIENTE] Erro ao abrir FIFO de resposta para leitura");
        unlink(client_response_fifo);
        return 1;
    }
    
    ControllerResponse response;
    
    // Leitura da estrutura de resposta do Controlador
    if (read(response_fd, &response, sizeof(ControllerResponse)) > 0) {
        if (response.success) {
            printf("[CLIENTE %s] LOGIN SUCESSO: %s\n", username, response.message);
        } else {
            printf("[CLIENTE %s] LOGIN FALHA: %s\n", username, response.message);
        }
    } else {
        printf("[CLIENTE %s] Controlador encerrou a ligação sem resposta. A sair.\n", username);
    }
    
    printf("\n[CLIENTE %s] Login completo. Digite 'ajuda' para ver comandos.\n", username);
    
    char command_buffer[MAX_MESSAGE * 2]; // Buffer para comandos
    
    // O Cliente precisa de ler comandos do teclado (File Descriptor 0) E 
    // mensagens do pipe do Controlador (response_fd)
    
    while (1) {
        // Por enquanto, vamos simplificar e apenas ler comandos do teclado (stdin - fd 0)
        // A leitura simultânea será adicionada depois.

        printf("> ");
        fflush(stdout); // Garante que o prompt aparece

        // A função fgets lê do teclado (stdin)
        if (fgets(command_buffer, sizeof(command_buffer), stdin) == NULL) {
            // Se o utilizador digitar Ctrl+D, o programa deve sair.
            printf("\n[CLIENTE %s] Entrada de dados terminada. A sair.\n", username);
            break; 
        }

        // Remover a quebra de linha ('\n') lida pelo fgets
        command_buffer[strcspn(command_buffer, "\n")] = 0; 

        // 1. Processar o comando de terminação
        if (strcmp(command_buffer, "terminar") == 0) {
            // Lógica: Se não houver serviço em execução, pode terminar
            printf("[CLIENTE %s] Comando 'terminar' recebido. A fechar...\n", username);
            
            // Aqui seria necessário avisar o Controlador para cancelar agendamentos.
            // Por enquanto, saímos.
            break; 
        }

        // 2. Lógica para outros comandos (enviar para Controlador ou Veículo)
        if (strlen(command_buffer) > 0) {
            printf("[CLIENTE %s] Comando '%s' não implementado ainda. Tente 'terminar'.\n", username, command_buffer);
            // Aqui teria que enviar o comando para o Controlador através do FIFO de pedidos.
        }

        // ... (Faltará a lógica para ler do response_fd aqui!) ...
    }
    
    //----------------------------------------------------------------------
    // LIMPEZA FINAL
    //----------------------------------------------------------------------

    // Fechar descritores abertos
    close(response_fd);
    
    // Remover o FIFO pessoal
    unlink(client_response_fifo); 
    
    printf("[CLIENTE %s] Programa encerrado de forma ordenada.\n", username);
    
    return 0;
}