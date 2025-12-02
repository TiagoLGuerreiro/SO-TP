// comum.h
#ifndef COMUM_H
#define COMUM_H

#define MAX_USERNAME 30
#define MAX_PIPE_NAME 50
#define MAX_MESSAGE 100

// Tipos de comandos
#define CMD_LOGIN 1
#define CMD_AGENDAR 2
#define CMD_SAIR 3

// Estrutura enviada pelo Cliente -> Controlador
typedef struct {
    int command_type;                 // Tipo do comando (ex: CMD_LOGIN)
    int pid;                          // PID do cliente
    char username[MAX_USERNAME];      // Nome do utilizador
    char response_pipe_name[MAX_PIPE_NAME]; // Pipe para resposta
    char data[MAX_MESSAGE];           // Outros dados (opcional)
} ClientRequest;

// Estrutura enviada pelo Controlador -> Cliente
typedef struct {
    int success;                      // 1 = Sucesso, 0 = Erro
    char message[MAX_MESSAGE];        // Mensagem explicativa
} ControllerResponse;

#endif