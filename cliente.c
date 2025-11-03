#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#define FIFO_CLIENTE "/tmp/fifo_cliente" //caminho do FIFO do cliente

void enviar_comando(const char *mensagem) { //função para enviar comando ao controlador
    int fd = open(FIFO_CLIENTE, O_WRONLY); // abre FIFO para escrever
    if (fd < 0) { // se der erro ao abrir o FIFO
        perror("Cliente: Erro ao abrir FIFO"); //mostra que deu erro
        return;
    }
    write(fd, mensagem, strlen(mensagem) + 1); // inclui o '\0'
    close(fd); // fecha o FIFO
}

int main(int argc, char *argv[]) {
    printf("Ola. A enviar comando de teste...\n"); //mensagem de teste
    enviar_comando("AGENDAR 12 5km LISBOA\n"); //envia comando de agendar
    return 0;
}
