#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "comum.h" // Importante!

#define FIFO_CONTROLLER_REQUESTS "/tmp/SO_TAXI_PEDIDOS"
#define MAX_CLIENTES 30 // Limite do enunciado

// Estrutura para guardar clientes no servidor (memória interna)
typedef struct
{
    char username[MAX_USERNAME];
    int pid;
    int ocupado; // 0 = livre, 1 = ocupado
} ClienteRegistado;

// Lista de clientes logados
ClienteRegistado frota_clientes[MAX_CLIENTES];

// Inicializa a lista de clientes a zero
void init_clientes()
{
    for (int i = 0; i < MAX_CLIENTES; i++)
    {
        frota_clientes[i].ocupado = 0;
    }
}

// Tenta registar um cliente. Retorna 1 se sucesso, 0 se erro (duplicado ou cheio)
int registar_cliente(char *username, int pid, char *razao_erro)
{
    // 1. Verificar duplicados
    for (int i = 0; i < MAX_CLIENTES; i++)
    {
        if (frota_clientes[i].ocupado && strcmp(frota_clientes[i].username, username) == 0)
        {
            strcpy(razao_erro, "Username ja existe.");
            return 0; // Falha
        }
    }

    // 2. Encontrar espaço livre
    for (int i = 0; i < MAX_CLIENTES; i++)
    {
        if (!frota_clientes[i].ocupado)
        {
            frota_clientes[i].ocupado = 1;
            strcpy(frota_clientes[i].username, username);
            frota_clientes[i].pid = pid;
            return 1; // Sucesso
        }
    }

    strcpy(razao_erro, "Sistema cheio (Max 30 clientes).");
    return 0; // Cheio
}

void remover_cliente(int pid)
{
    for (int i = 0; i < MAX_CLIENTES; i++)
    {
        if (frota_clientes[i].ocupado && frota_clientes[i].pid == pid)
        {
            frota_clientes[i].ocupado = 0;
            printf("[LOGOUT] Cliente %s (PID %d) saiu.\n", frota_clientes[i].username, pid);
            return;
        }
    }
}

void cliente_com()
{
    init_clientes(); // Limpa a memória de clientes

    unlink(FIFO_CONTROLLER_REQUESTS);
    if (mkfifo(FIFO_CONTROLLER_REQUESTS, 0666) == -1)
    {
        perror("Erro mkfifo");
        exit(1);
    }

    int fd_req = open(FIFO_CONTROLLER_REQUESTS, O_RDWR); // O_RDWR evita que o read retorne 0 logo que o cliente fecha
    if (fd_req == -1)
    {
        perror("Erro open");
        exit(1);
    }

    printf("[CONTROLADOR] A aguardar pedidos...\n");

    ClientRequest req;
    ControllerResponse resp;

    while (1)
    {
        // Lê o tamanho exato da estrutura
        if (read(fd_req, &req, sizeof(ClientRequest)) == sizeof(ClientRequest))
        {

            if (req.command_type == CMD_LOGIN)
            {
                printf("[LOGIN] Pedido de: %s (PID %d)\n", req.username, req.pid);

                char erro[MAX_MESSAGE];
                if (registar_cliente(req.username, req.pid, erro))
                {
                    // SUCESSO
                    resp.success = 1;
                    sprintf(resp.message, "Bem-vindo, %s!", req.username);
                    printf("[LOGIN] Sucesso: %s entrou.\n", req.username);
                }
                else
                {
                    // FALHA
                    resp.success = 0;
                    strcpy(resp.message, erro);
                    printf("[LOGIN] Recusado (%s): %s\n", req.username, erro);
                }

                // Enviar resposta
                int fd_resp = open(req.response_pipe_name, O_WRONLY);
                if (fd_resp != -1)
                {
                    write(fd_resp, &resp, sizeof(ControllerResponse));
                    close(fd_resp);
                }
            }
            else if (req.command_type == CMD_SAIR)
            {
                // Lógica de logout correta!
                remover_cliente(req.pid);
            }
        }
    }
}

int main()
{
    // veiculo_start_ler_telemetria(); // Podes descomentar depois
    cliente_com();
    return 0;
}