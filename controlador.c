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
#define MAX_CLIENTES 30 // Limite do enunciado
#define MAX_SERVICOS 100 // Um número razoável para a fila de espera

// Estrutura para guardar clientes no servidor (memória interna)
typedef struct
{
    char username[MAX_USERNAME];
    int pid;
    char pipe_nome[MAX_PIPE_NAME]; // <--- NOVO CAMPO ÚTIL
    int ocupado; // 0 = livre, 1 = ocupado
} ClienteRegistado;

// 2. ESTRUTURA PARA SERVIÇOS (VIAGENS AGENDADAS)
typedef struct {
    int id;                 // ID único do serviço
    char username[MAX_USERNAME];
    int pid_cliente;        // Quem pediu
    char pipe_cliente[MAX_PIPE_NAME]; // Onde responder
    int hora_agendada;      // Quando
    int distancia;          // Quanto
    char local[50];         // Onde
    int estado;             // 0=Agendado, 1=Em Execução, 2=Concluído
    int fd_telemetria;      // Para ler do veículo
} Servico;

// VARIÁVEIS GLOBAIS
ClienteRegistado frota_clientes[MAX_CLIENTES]; // Lista de Pessoas
Servico lista_servicos[MAX_SERVICOS];          // Lista de Viagens
int total_servicos = 0;                        // Contador de IDs

// Inicializa a lista de clientes a zero
void init_clientes()
{
    for (int i = 0; i < MAX_CLIENTES; i++)
    {
        frota_clientes[i].ocupado = 0;
    }
}

// Tenta registar um cliente. Retorna 1 se sucesso, 0 se erro (duplicado ou cheio)
int registar_cliente(char* username, int pid, char* pipe_cliente, char* razao_erro) {
    // 1. Verificar duplicados (Igual a antes)
    for(int i=0; i<MAX_CLIENTES; i++) {
        if (frota_clientes[i].ocupado && strcmp(frota_clientes[i].username, username) == 0) {
            strcpy(razao_erro, "Username ja existe.");
            return 0;
        }
    }

    // 2. Encontrar espaço livre
    for(int i=0; i<MAX_CLIENTES; i++) {
        if (!frota_clientes[i].ocupado) {
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
int agendar_servico(char* username, int pid, char* pipe_resp, char* dados_str) {
    // 1. Procurar slot livre
    int slot = -1;
    for(int i=0; i<MAX_SERVICOS; i++) {
        // Vamos simplificar: se o ID for 0, o slot está livre (assumindo IDs > 0)
        if (lista_servicos[i].id == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) return -1; // Lista cheia

    // 2. Parse da string de dados: "HORA LOCAL DISTANCIA"
    // Nota: O teu cliente envia: "agendar 12 Coimbra 5" -> dados_str = "12 Coimbra 5"
    // Atenção à ordem definida no enunciado: <hora> <local> <distancia> [cite: 754]
    
    int hora, distancia;
    char local[50];
    
    // sscanf lê da string formatada
    if (sscanf(dados_str, "%d %s %d", &hora, local, &distancia) != 3) {
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
void iniciar_viagem(int index_servico) {
    Servico *s = &lista_servicos[index_servico];
    int pipe_fd[2];

    if (pipe(pipe_fd) == -1) {
        perror("[ERRO] Criar pipe veiculo");
        return;
    }

    pid_t pid = fork();

    if (pid == 0) { // FILHO (Veículo)
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
    else if (pid > 0) { // PAI (Controlador)
        close(pipe_fd[1]); // Fecha escrita
        
        // --- ALTERAÇÕES AQUI ---
        s->estado = 1; // Marca como Em Execução
        s->fd_telemetria = pipe_fd[0]; // Guarda o pipe para ler depois
        
        // Colocar o pipe em modo NONBLOCK para não travar o loop principal
        int flags = fcntl(s->fd_telemetria, F_GETFL, 0);
        fcntl(s->fd_telemetria, F_SETFL, flags | O_NONBLOCK);

        printf("[CONTROLADOR] Veículo lançado para serviço %d (PID %d)\n", s->id, pid);
    }
}

void verificar_agendamentos(int instante_atual) {
    for(int i=0; i<MAX_SERVICOS; i++) {
        // Se o serviço existe, está apenas "Agendado" (0) e a hora chegou
        if (lista_servicos[i].id != 0 && lista_servicos[i].estado == 0) {
            if (lista_servicos[i].hora_agendada <= instante_atual) {
                iniciar_viagem(i);
            }
        }
    }
}

void ler_telemetria_veiculos() {
    char buffer[256];
    
    for(int i=0; i<MAX_SERVICOS; i++) {
        // Só nos interessa ler de serviços que estão "Em Execução" (estado 1)
        if (lista_servicos[i].id != 0 && lista_servicos[i].estado == 1) {
            
            ssize_t n = read(lista_servicos[i].fd_telemetria, buffer, sizeof(buffer)-1);
            
            if (n > 0) {
                buffer[n] = '\0';
                // Imprime o que o veículo disse (Ex: "10% concluido")
                printf("[TELEMETRIA Servico %d]: %s", lista_servicos[i].id, buffer);
            } 
            else if (n == 0) {
                // Se read retornar 0, significa que o veículo fechou o pipe (terminou/morreu)
                printf("[CONTROLADOR] Veículo do serviço %d terminou a ligação.\n", lista_servicos[i].id);
                close(lista_servicos[i].fd_telemetria);
                lista_servicos[i].estado = 2; // Marca como Concluído
            }
        }
    }
}

void cliente_com()
{
    init_clientes(); // Limpa a memória de clientes
    
    // Assegura que começamos com um pipe limpo
    unlink(FIFO_CONTROLLER_REQUESTS);
    if (mkfifo(FIFO_CONTROLLER_REQUESTS, 0666) == -1) {
        perror("[ERRO] mkfifo");
        exit(1);
    }

    // ABRIR EM MODO NÃO BLOQUEANTE (O_NONBLOCK)
    // O_RDWR é usado para manter o pipe aberto mesmo sem escritores (evita EOF constante)
    int fd_req = open(FIFO_CONTROLLER_REQUESTS, O_RDWR | O_NONBLOCK);
    if (fd_req == -1) {
        perror("[ERRO] open FIFO");
        exit(1);
    }

    printf("[CONTROLADOR] A iniciar simulação. Tempo T=0.\n");

    ClientRequest req;
    ControllerResponse resp;
    int instante_atual = 0;

    while (1)
    {
        // 1. TENTAR LER PEDIDOS (Não bloqueia se estiver vazio)
        ssize_t n = read(fd_req, &req, sizeof(ClientRequest));

        if (n == sizeof(ClientRequest)) 
        {
            // --- TRATAMENTO DE COMANDOS ---

            // === LOGIN ===
            if (req.command_type == CMD_LOGIN) 
            {
                printf("[LOGIN] Pedido de: %s (PID %d)\n", req.username, req.pid);
                char erro[MAX_MESSAGE];
                
                // Passamos também o pipe para registo (req.response_pipe_name)
                if (registar_cliente(req.username, req.pid, req.response_pipe_name, erro)) {
                    resp.success = 1;
                    sprintf(resp.message, "Bem-vindo! T atual do sistema: %d", instante_atual);
                    printf("[LOGIN] Sucesso: %s entrou.\n", req.username);
                } else {
                    resp.success = 0;
                    strcpy(resp.message, erro);
                    printf("[LOGIN] Recusado (%s): %s\n", req.username, erro);
                }
                
                // Responder ao cliente
                int fd_resp = open(req.response_pipe_name, O_WRONLY);
                if (fd_resp != -1) {
                    write(fd_resp, &resp, sizeof(ControllerResponse));
                    close(fd_resp);
                }
            }
            // === AGENDAR ===
            else if (req.command_type == CMD_AGENDAR)
            {
                // Chama a função que criaste para guardar o serviço
                int id = agendar_servico(req.username, req.pid, req.response_pipe_name, req.data);
                
                if (id > 0) {
                    resp.success = 1;
                    sprintf(resp.message, "Agendado ID %d com sucesso.", id);
                } else {
                    resp.success = 0;
                    sprintf(resp.message, "Erro ao agendar (ID: %d). Verifique formato.", id);
                }

                // Responder ao cliente
                int fd_resp = open(req.response_pipe_name, O_WRONLY);
                if (fd_resp != -1) {
                    write(fd_resp, &resp, sizeof(ControllerResponse));
                    close(fd_resp);
                }
            }
            // === SAIR ===
            else if (req.command_type == CMD_SAIR) 
            {
                remover_cliente(req.pid);
            }
        }

        // 2. SIMULAÇÃO DO TEMPO E VEÍCULOS
        
        // Verifica se há serviços para lançar AGORA (neste instante)
        verificar_agendamentos(instante_atual);

        ler_telemetria_veiculos();

        // Avança o relógio
        sleep(1); 
        instante_atual++;
        
        // Opcional: Mostrar relógio a cada 5 ou 10 segundos para não poluir o ecrã
        if (instante_atual % 10 == 0) {
            printf("[RELÓGIO] T=%d\n", instante_atual);
        }
    }

    close(fd_req);
    unlink(FIFO_CONTROLLER_REQUESTS);
}

int main()
{
    // veiculo_start_ler_telemetria(); // Podes descomentar depois
    cliente_com();
    return 0;
}