#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);

    if(argc < 4){
        fprintf(stderr, "Veículo: Erro de argumentos\n"); // se nao tiver argumentos suficientes manda para o stderr
        return 1;
    }

    int id_servico = atoi(argv[1]);
    int km_distancia = atoi(argv[2]);

    // simula inicio de serviço e envia a primeira telemetria
    printf("[VEICULO %d] Recebido. Distância: %d km.\n", id_servico, km_distancia);
    printf("[VEICULO %d] A caminho do cliente.\n", id_servico);

    for (int i = 1; i <= 3; i++){
        sleep(1); //simula a viagem esperando 1 segundo
        printf("[VEICULO %d] TELEMETRIA: %d%% concluido.\n", id_servico, i * 10);
    }

    printf("[VEICULO %d] Serviço concluido. A terminar.\n", id_servico);

    return 0;
}