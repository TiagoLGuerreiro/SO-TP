#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/select.h>
#include "comum.h"

// Variáveis globais para o handler aceder
int id_servico_global = 0;

// Função que é ativada quando chega o sinal SIGUSR1
void trata_cancelamento(int sinal) {
    // Escreve uma mensagem para saberes que funcionou
    fprintf(stderr, "\n[VEICULO %d] Recebi ordem de cancelamento (Sinal %d)!\n", getpid(), sinal);
    fprintf(stderr, "[VEICULO %d] A abortar tarefa e a regressar à base... Adeus!\n", getpid());

    exit(0); // Termina o processo do veículo imediatamente
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);

    if(argc < 4){
        fprintf(stderr, "Veículo: Erro de argumentos\n");
        return 1;
    }

    id_servico_global = atoi(argv[1]);
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
    
    int fd_veiculo = open(fifo_veiculo, O_RDWR);
    char buffer[MAX_MESSAGE];
    
    // Ler do pipe. Esperamos algo como "entrar <destino>"
    if (read(fd_veiculo, buffer, sizeof(buffer)) > 0) {
        printf("[VEICULO %d] Cliente disse: %s. A iniciar viagem!\n", id_servico_global, buffer);
    }

    // 4. Iniciar Viagem
    printf("[VEICULO %d] A iniciar viagem de %d km...\n", id_servico_global, km_distancia);

    // Calcular tempo por cada 10% (velocidade = 1km/s)
    // Ex: 30km -> 30s total -> 3s por iteração
    int intervalo_segundos = km_distancia / 10;
    if (intervalo_segundos < 1) intervalo_segundos = 1; // Mínimo de segurança

    int viagem_cancelada = 0;

    for (int i = 1; i <= 10; i++) {
        
        // --- PREPARAR O SELECT ---
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd_veiculo, &read_fds); // Vamos vigiar o pipe

        struct timeval timeout;
        timeout.tv_sec = intervalo_segundos;
        timeout.tv_usec = 0;

        // "Select, espera aí até chegar dados OU o tempo acabar"
        int resultado = select(fd_veiculo + 1, &read_fds, NULL, NULL, &timeout);

        if (resultado == 0) {
            // TIMEOUT: Passou o tempo, avançamos a viagem
            printf("[VEICULO %d] TELEMETRIA: %d%% concluido.\n", id_servico_global, i * 10);
        }
        else if (resultado > 0) {
            // DADOS RECEBIDOS: O cliente falou!
            if (FD_ISSET(fd_veiculo, &read_fds)) {
                char cmd[MAX_MESSAGE];
                int n = read(fd_veiculo, cmd, sizeof(cmd));
                if (n > 0) {
                    cmd[n] = '\0'; // Garantir terminação da string
                    
                    // Verificar se é o comando "sair"
                    if (strncmp(cmd, "sair", 4) == 0) {
                        printf("[VEICULO %d] O cliente pediu para sair a meio!\n", id_servico_global);
                        viagem_cancelada = 1;
                        break; // Sai do ciclo for imediatamente
                    }
                }
            }
        }
        else {
            perror("[VEICULO] Erro no select");
            break;
        }
    }

    // --- FIM DA VIAGEM ---
    if (viagem_cancelada) {
        printf("[VEICULO %d] Viagem terminada prematuramente.\n", id_servico_global);
    } else {
        printf("[VEICULO %d] Cheguei ao destino! Viagem concluida.\n", id_servico_global);
    }

    // AGORA SIM, fechamos tudo
    close(fd_veiculo);
    unlink(fifo_veiculo);
    
    return 0;
}