#ifndef COMUM_H
#define COMUM_H

#define MAX_USERNAME 30
#define MAX_PIPE_NAME 50
#define MAX_MESSAGE 100

// -----------------------------------------------------
// ESTRUTURA DE PEDIDO (CLIENTE -> CONTROLADOR)
// O Cliente usa isto para enviar pedidos de Login, Agendamento, etc.
// -----------------------------------------------------
typedef struct {
    int pid;
    char username[MAX_USERNAME];
    // O nome do pipe pessoal para o Controlador responder (bidirecionalidade)
    char response_pipe_name[MAX_PIPE_NAME]; 
    // Campos para identificar o comando (usaremos mais tarde)
    int command_type; // Ex: 1 para LOGIN, 2 para AGENDAR, etc.
    char data[MAX_MESSAGE]; // Dados do comando (ex: "12 5km LISBOA")
} ClientRequest;


// -----------------------------------------------------
// ESTRUTURA DE RESPOSTA (CONTROLADOR -> CLIENTE)
// O Controlador usa isto para enviar confirmações/notificações.
// -----------------------------------------------------
typedef struct {
    int success; // 1 = OK, 0 = FALHA
    char message[MAX_MESSAGE];
} ControllerResponse;

// Tipos de comandos (constantes)
#define CMD_LOGIN 1
#define CMD_AGENDAR 2

#endif