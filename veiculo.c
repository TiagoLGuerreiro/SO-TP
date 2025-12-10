#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h> // Necessário para sinais
#include "comum.h"

// Variáveis globais para o handler aceder
int id_servico_global = 0;

// Função que é ativada quando chega o sinal SIGUSR1
void trata_cancelamento(int sinal) {
    // Escreve uma mensagem para saberes que funcionou
    printf("\n[VEICULO %d] Recebi ordem de cancelamento (Sinal %d)!\n", getpid(), sinal);
    printf("[VEICULO %d] A abortar tarefa e a regressar à base... Adeus!\n", getpid());

    exit(0); // Termina o processo do veículo imediatamente
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);

    if(argc < 4){
        fprintf(stderr, "Veículo: Erro de argumentos\n");
        return 1;
    }

    int id_servico_global = atoi(argv[1]);
    int km_distancia = atoi(argv[2]);
    char *pipe_cliente = argv[3]; // O pipe para onde vamos mandar o "Cheguei"

    // --- CONFIGURAÇÃO DO SINAL (O código da Ficha 4) ---
    struct sigaction sa;
    sa.sa_handler = trata_cancelamento;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // Tenta registrar a ação para o sinal SIGUSR1
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("[VEICULO] Erro ao configurar sigaction");
        exit(1);
    }

    // 1. Criar o FIFO do Veículo (para receber o comando 'entrar')
    char fifo_veiculo[MAX_PIPE_NAME];
    sprintf(fifo_veiculo, "/tmp/veiculo_%d", getpid());
    mkfifo(fifo_veiculo, 0666);

    printf("[VEICULO %d] Iniciei. Destino base: %d km.\n", id_servico_global, km_distancia);

    // 2. Avisar o Cliente que cheguei (Usamos ControllerResponse para o cliente entender)
    ControllerResponse notificacao;
    notificacao.success = 1;
    // Formatamos a mensagem com um prefixo especial para o cliente saber processar
    // Formato: "CHEGUEI <nome_do_pipe_do_veiculo>"
    sprintf(notificacao.message, "CHEGUEI %s", fifo_veiculo);

    int fd_cli = open(pipe_cliente, O_WRONLY);
    if (fd_cli != -1) {
        write(fd_cli, &notificacao, sizeof(ControllerResponse));
        close(fd_cli);
    } else {
        printf("[VEICULO %d] Erro ao contactar cliente no pipe %s\n", id_servico_global, pipe_cliente);
        // Se falhar, continuamos ou terminamos? Vamos continuar para testes.
    }

    // 3. Esperar pelo comando "entrar" do Cliente
    printf("[VEICULO %d] A aguardar cliente...\n", id_servico_global);
    
    int fd_veiculo = open(fifo_veiculo, O_RDONLY);
    char buffer[MAX_MESSAGE];
    
    // Ler do pipe. Esperamos algo como "entrar <destino>"
    if (read(fd_veiculo, buffer, sizeof(buffer)) > 0) {
        printf("[VEICULO %d] Cliente disse: %s. A iniciar viagem!\n", id_servico_global, buffer);
    }
    close(fd_veiculo);
    unlink(fifo_veiculo); // Apagar o pipe, já não precisamos dele para a viagem

    // 4. Iniciar Viagem (Telemetria)
    printf("[VEICULO %d] A caminho do destino...\n", id_servico_global);

    for (int i = 1; i <= 10; i++){
        sleep(1); 
        printf("[VEICULO %d] TELEMETRIA: %d%% concluido.\n", id_servico_global, i * 10);
    }

    printf("[VEICULO %d] Serviço concluido. A terminar.\n", id_servico_global);

    return 0;
}