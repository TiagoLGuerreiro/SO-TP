#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "comum.h"
#include <errno.h>

#define FIFO_CONTROLLER_REQUESTS "/tmp/SO_TAXI_PEDIDOS"
#define MAX_CLIENTES 30  // Limite do enunciado
#define MAX_SERVICOS 100 // Um número razoável para a fila de espera

// Estrutura para guardar clientes no servidor (memória interna)
typedef struct
{
    char username[MAX_USERNAME];
    int pid;
    char pipe_nome[MAX_PIPE_NAME]; // <--- NOVO CAMPO ÚTIL
    int ocupado;                   // 0 = livre, 1 = ocupado
} ClienteRegistado;

// 2. ESTRUTURA PARA SERVIÇOS (VIAGENS AGENDADAS)
typedef struct
{
    int id; // ID único do serviço
    char username[MAX_USERNAME];
    int pid_cliente;                  // Quem pediu
    char pipe_cliente[MAX_PIPE_NAME]; // Onde responder
    int hora_agendada;                // Quando
    int distancia;                    // Quanto
    char local[50];                   // Onde
    int estado;                       // 0=Agendado, 1=Em Execução, 2=Concluído
    int fd_telemetria;                // Para ler do veículo
    int pid_veiculo;
} Servico;

// VARIÁVEIS GLOBAIS
ClienteRegistado frota_clientes[MAX_CLIENTES]; // Lista de Pessoas
Servico lista_servicos[MAX_SERVICOS];          // Lista de Viagens
int total_servicos = 0;                        // Contador de IDs
long long total_km_percorridos = 0;            // COMANDO 'km'
int MAX_VEICULOS_SIMULTANEOS = 0;              // Limite veiculos

// Inicializa a lista de clientes a zero
void init_clientes()
{
    for (int i = 0; i < MAX_CLIENTES; i++)
    {
        frota_clientes[i].ocupado = 0;
    }
}

// Tenta registar um cliente. Retorna 1 se sucesso, 0 se erro (duplicado ou cheio)
int registar_cliente(char *username, int pid, char *pipe_cliente, char *razao_erro)
{
    // 1. Verificar duplicados (Igual a antes)
    for (int i = 0; i < MAX_CLIENTES; i++)
    {
        if (frota_clientes[i].ocupado && strcmp(frota_clientes[i].username, username) == 0)
        {
            strcpy(razao_erro, "Username ja existe.");
            return 0;
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
            // GUARDAR O PIPE AQUI:
            strcpy(frota_clientes[i].pipe_nome, pipe_cliente);
            return 1;
        }
    }

    strcpy(razao_erro, "Sistema cheio.");
    return 0;
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

// Retorna o ID do serviço criado ou -1 em caso de erro
int agendar_servico(char *username, int pid, char *pipe_resp, char *dados_str)
{
    // 1. Procurar slot livre
    int slot = -1;
    for (int i = 0; i < MAX_SERVICOS; i++)
    {
        // Vamos simplificar: se o ID for 0, o slot está livre (assumindo IDs > 0)
        if (lista_servicos[i].id == 0)
        {
            slot = i;
            break;
        }
    }

    if (slot == -1)
        return -1; // Lista cheia

    // 2. Parse da string de dados: "HORA LOCAL DISTANCIA"
    // Nota: O teu cliente envia: "agendar 12 Coimbra 5" -> dados_str = "12 Coimbra 5"
    // Atenção à ordem definida no enunciado: <hora> <local> <distancia> [cite: 754]

    int hora, distancia;
    char local[50];

    // sscanf lê da string formatada
    if (sscanf(dados_str, "%d %s %d", &hora, local, &distancia) != 3)
    {
        return -2; // Erro de formatação
    }

    // 3. Guardar na estrutura
    lista_servicos[slot].id = ++total_servicos; // Gera ID (1, 2, 3...)
    strcpy(lista_servicos[slot].username, username);
    lista_servicos[slot].pid_cliente = pid;
    strcpy(lista_servicos[slot].pipe_cliente, pipe_resp);
    lista_servicos[slot].hora_agendada = hora;
    strcpy(lista_servicos[slot].local, local);
    lista_servicos[slot].distancia = distancia;
    lista_servicos[slot].estado = 0; // Agendado

    printf("[AGENDAMENTO] Serviço %d agendado para T=%d por %s.\n",
           lista_servicos[slot].id, hora, username);

    return lista_servicos[slot].id;
}

// Função que lança o processo Veículo para um serviço específico
void iniciar_viagem(int index_servico)
{
    Servico *s = &lista_servicos[index_servico];
    int pipe_fd[2];

    if (pipe(pipe_fd) == -1)
    {
        perror("[ERRO] Criar pipe veiculo");
        return;
    }

    pid_t pid = fork();

    if (pid == 0)
    { // FILHO (Veículo)
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO); // Redireciona stdout
        close(pipe_fd[1]);

        char arg_id[10], arg_dist[10];
        sprintf(arg_id, "%d", s->id);
        sprintf(arg_dist, "%d", s->distancia);

        // Passamos também o pipe do cliente para o veículo falar diretamente com ele depois (opcional, mas bom ter)
        execl("./veiculo", "./veiculo", arg_id, arg_dist, s->pipe_cliente, (char *)NULL);
        perror("[ERRO] Falha no execl");
        exit(1);
    }
    else if (pid > 0)
    { // PAI (Controlador)
        close(pipe_fd[1]);

        s->estado = 1;
        s->fd_telemetria = pipe_fd[0];
        s->pid_veiculo = pid; // <--- GUARDAR O PID AQUI!

        int flags = fcntl(s->fd_telemetria, F_GETFL, 0);
        fcntl(s->fd_telemetria, F_SETFL, flags | O_NONBLOCK);

        printf("[CONTROLADOR] Veículo lançado para serviço %d (PID %d)\n", s->id, pid);
    }
}

void verificar_agendamentos(int instante_atual)
{
    // 1. Contar quantos veículos estão ativos neste momento
    int veiculos_ativos = 0;
    for (int i = 0; i < MAX_SERVICOS; i++) {
        if (lista_servicos[i].id != 0 && lista_servicos[i].estado == 1) {
            veiculos_ativos++;
        }
    }

    // 2. percorrer os agendamentos
    for (int i = 0; i < MAX_SERVICOS; i++)
    {
        // Se existe e está "Agendado" (estado 0)
        if (lista_servicos[i].id != 0 && lista_servicos[i].estado == 0)
        {
            // Se já chegou a hora
            if (lista_servicos[i].hora_agendada <= instante_atual)
            {
                // SÓ LANÇA SE HOUVER VAGAS NA FROTA
                if (veiculos_ativos < MAX_VEICULOS_SIMULTANEOS) {
                    iniciar_viagem(i);
                    veiculos_ativos++; // Incrementa para não lançar demais no mesmo ciclo!
                }
            }
        }
    }
}

void ler_telemetria_veiculos()
{
    char buffer[256];

    for (int i = 0; i < MAX_SERVICOS; i++)
    {
        // Só nos interessa ler de serviços que estão "Em Execução" (estado 1)
        if (lista_servicos[i].id != 0 && lista_servicos[i].estado == 1)
        {

            ssize_t n = read(lista_servicos[i].fd_telemetria, buffer, sizeof(buffer) - 1);

            if (n > 0)
            {
                buffer[n] = '\0';
                // Imprime o que o veículo disse (Ex: "10% concluido")
                printf("[TELEMETRIA Servico %d]: %s", lista_servicos[i].id, buffer);
            }
            else if (n == 0)
            {
                // Se read retornar 0, significa que o veículo fechou o pipe (terminou/morreu)
                printf("[CONTROLADOR] Veículo do serviço %d terminou a ligação.\n", lista_servicos[i].id);
                close(lista_servicos[i].fd_telemetria);
                lista_servicos[i].estado = 2; // Marca como Concluído

                total_km_percorridos += lista_servicos[i].distancia;
            }
        }
    }
}

void encerrar_sistema() {
    printf("\n[SISTEMA] A encerrar... A limpar recursos.\n");

    // 1. Mandar parar todos os veículos ativos
    for (int i = 0; i < MAX_SERVICOS; i++) {
        if (lista_servicos[i].id != 0 && lista_servicos[i].estado == 1) {
            kill(lista_servicos[i].pid_veiculo, SIGUSR1);
            printf("[SISTEMA] Sinal de paragem enviado ao veículo %d\n", lista_servicos[i].pid_veiculo);
        }
    }

    // 2. Avisar Clientes (Requisito do enunciado)
    // Como guardaste o pipe_nome na struct ClienteRegistado, podes usar isso!
    ControllerResponse resp;
    resp.success = 0;
    strcpy(resp.message, "O sistema vai encerrar. Adeus!");
    
    for(int i=0; i<MAX_CLIENTES; i++) {
        if(frota_clientes[i].ocupado) {
            int fd = open(frota_clientes[i].pipe_nome, O_WRONLY | O_NONBLOCK);
            if(fd != -1) {
                write(fd, &resp, sizeof(ControllerResponse));
                close(fd);
            }
        }
    }

    // 3. Apagar o FIFO do controlador
    unlink(FIFO_CONTROLLER_REQUESTS);
    
    printf("[SISTEMA] Encerrado com sucesso.\n");
    exit(0);
}

// Handler para o Ctrl+C
void trata_ctrl_c(int sinal) {
    encerrar_sistema();
}


void cliente_com()
{
    init_clientes();

    unlink(FIFO_CONTROLLER_REQUESTS);
    if (mkfifo(FIFO_CONTROLLER_REQUESTS, 0666) == -1)
    {
        perror("[ERRO] mkfifo");
        exit(1);
    }

    // 1. Configurar FIFO para NÃO BLOQUEANTE
    int fd_req = open(FIFO_CONTROLLER_REQUESTS, O_RDWR | O_NONBLOCK);
    if (fd_req == -1)
    {
        perror("[ERRO] open FIFO");
        exit(1);
    }

    // 2. Configurar TECLADO (STDIN) para NÃO BLOQUEANTE <--- NOVO
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    printf("[CONTROLADOR] A iniciar. Comandos Admin: listar, terminar.\n");

    ClientRequest req;
    ControllerResponse resp;
    int instante_atual = 0;
    char cmd_buffer[100]; // Buffer para o administrador
    
    signal(SIGINT, trata_ctrl_c);
    
    while (1)
    {
        // --- A) LER PEDIDOS DOS CLIENTES (FIFO) ---
        ssize_t n = read(fd_req, &req, sizeof(ClientRequest));

        if (n == sizeof(ClientRequest))
        {
            if (req.command_type == CMD_LOGIN)
            {
                char erro[MAX_MESSAGE];
                printf("[LOGIN] Pedido de: %s (PID %d)\n", req.username, req.pid);
                if (registar_cliente(req.username, req.pid, req.response_pipe_name, erro))
                {
                    resp.success = 1;
                    sprintf(resp.message, "Bem-vindo! T=%d", instante_atual);
                }
                else
                {
                    resp.success = 0;
                    strcpy(resp.message, erro);
                }
                int fd_resp = open(req.response_pipe_name, O_WRONLY);
                if (fd_resp != -1)
                {
                    write(fd_resp, &resp, sizeof(ControllerResponse));
                    close(fd_resp);
                }
            }
            else if (req.command_type == CMD_AGENDAR)
            {
                int id = agendar_servico(req.username, req.pid, req.response_pipe_name, req.data);
                resp.success = (id > 0);
                if (id > 0)
                    sprintf(resp.message, "Agendado ID %d", id);
                else
                    strcpy(resp.message, "Erro agendar");

                int fd_resp = open(req.response_pipe_name, O_WRONLY);
                if (fd_resp != -1)
                {
                    write(fd_resp, &resp, sizeof(ControllerResponse));
                    close(fd_resp);
                }
            }
            else if (req.command_type == CMD_SAIR)
            {
                remover_cliente(req.pid);
            }
        }

        // --- B) LER COMANDOS DO ADMINISTRADOR (TECLADO) <--- NOVO ---
        ssize_t n_cmd = read(STDIN_FILENO, cmd_buffer, sizeof(cmd_buffer) - 1);
        if (n_cmd > 0)
        {
            cmd_buffer[n_cmd] = '\0';
            cmd_buffer[strcspn(cmd_buffer, "\n")] = 0; // Tira o \n

            if (strcmp(cmd_buffer, "listar") == 0)
            {
                printf("\n--- CLIENTES ---\n");
                for (int i = 0; i < MAX_CLIENTES; i++)
                    if (frota_clientes[i].ocupado)
                        printf(" > %s (PID %d)\n", frota_clientes[i].username, frota_clientes[i].pid);
                printf("--- SERVIÇOS ---\n");
                for (int i = 0; i < MAX_SERVICOS; i++)
                    if (lista_servicos[i].id != 0)
                        printf(" > ID:%d | %s | Estado:%d | T:%d\n", lista_servicos[i].id, lista_servicos[i].username, lista_servicos[i].estado, lista_servicos[i].hora_agendada);
                printf("----------------\n");
            } else if (strcmp(cmd_buffer, "utiliz") == 0) {
                printf("\n--- UTILIZADORES ---\n");
                for (int i = 0; i < MAX_CLIENTES; i++){
                    if (frota_clientes[i].ocupado){
                        char estado_str[20] = "LIVRE";
                        for(int j=0; j<MAX_SERVICOS; j++) {
                             // Se encontrar um serviço deste cliente que não esteja concluído
                             if(lista_servicos[j].id != 0 && lista_servicos[j].pid_cliente == frota_clientes[i].pid) {
                                 if(lista_servicos[j].estado == 0) strcpy(estado_str, "A ESPERA");
                                 if(lista_servicos[j].estado == 1) strcpy(estado_str, "EM VIAGEM");
                             }
                        }
                        printf(" > %s (PID %d) - [%s]\n", frota_clientes[i].username, frota_clientes[i].pid, estado_str);
                    }
                }
                printf("--------------------\n");
            } else if (strcmp(cmd_buffer, "frota") == 0) {
                printf("\n--- FROTA EM MOVIMENTO ---\n");
                int ativos = 0;
                for (int i = 0; i < MAX_SERVICOS; i++) {
                    // Só mostra serviços em execução (estado 1)
                    if (lista_servicos[i].id != 0 && lista_servicos[i].estado == 1) {
                        printf(" > Veículo PID %d | Serviço ID %d | Cliente: %s\n", lista_servicos[i].pid_veiculo, lista_servicos[i].id, lista_servicos[i].username);
                        ativos++;
                    }
                }
                if(ativos == 0) printf(" (Nenhum veículo em movimento)\n");
                printf("--------------------------\n");
            } else if (strcmp(cmd_buffer, "km") == 0) {
                printf("[SISTEMA] Total Km percorridos: %lld km\n", total_km_percorridos);
            } else if (strcmp(cmd_buffer, "hora") == 0) {
                printf("[SISTEMA] Tempo Simulado: T=%d\n", instante_atual);
            } else if (strncmp(cmd_buffer, "cancelar", 8) == 0) {
                int id_cancelar;
                if (sscanf(cmd_buffer, "cancelar %d", &id_cancelar) == 1){
                    int encontrado = 0;
                    for (int i = 0; i < MAX_SERVICOS; i++){
                        if (lista_servicos[i].id == id_cancelar){
                            encontrado = 1;

                            if (lista_servicos[i].estado == 1){
                                kill(lista_servicos[i].pid_veiculo, SIGUSR1);
                                printf("[ADMIN] Cancelamento enviado ao Veículo %d.\n", lista_servicos[i].pid_veiculo);
                                close(lista_servicos[i].fd_telemetria);
                            } else {
                                printf("[ADMIN] Serviço %d cancelado (não estava ativo).\n", id_cancelar);
                            }

                            // Limpar dados
                            lista_servicos[i].id = 0;
                            lista_servicos[i].estado = 0; // Ou o valor que usas para 'livre'
                            break;
                        }
                    }
                    if (!encontrado)
                        printf("[ADMIN] Serviço %d não encontrado.\n", id_cancelar);
                }
                else
                {
                    printf("[ADMIN] Uso: cancelar <id>\n");
                }
            }
            else if (strcmp(cmd_buffer, "terminar") == 0)
            {
                encerrar_sistema();
                break;
            }
            else
            {
                printf("[ADMIN] Comando desconhecido: %s\n", cmd_buffer);
            }
        }

        // --- C) SIMULAÇÃO ---
        verificar_agendamentos(instante_atual);
        ler_telemetria_veiculos();

        sleep(1);
        instante_atual++;
        if (instante_atual % 10 == 0)
            printf("[RELÓGIO] T=%d\n", instante_atual);
    }

    close(fd_req);
    unlink(FIFO_CONTROLLER_REQUESTS);
}

int main()
{
    // Tenta ler a variável de ambiente
    char *env_nveiculos = getenv("NVEICULOS");
    if (env_nveiculos == NULL) {
        printf("Aviso: NVEICULOS nao definido. A assumir 1.\n");
        MAX_VEICULOS_SIMULTANEOS = 1; // Valor default seguro
    } else {
        MAX_VEICULOS_SIMULTANEOS = atoi(env_nveiculos);
        if(MAX_VEICULOS_SIMULTANEOS > 10) 
            MAX_VEICULOS_SIMULTANEOS = 10; // Limite enunciado
    }

    cliente_com();
    return 0;
}