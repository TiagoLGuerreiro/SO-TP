#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#define FIFO_CLIENTE "/tmp/fifo_cliente"

void enviar_comando(const char *mensagem) {
    int fd = open(FIFO_CLIENTE, O_WRONLY);
    if (fd < 0) {
        perror("Cliente: Erro ao abrir FIFO");
        return;
    }
    write(fd, mensagem, strlen(mensagem) + 1); // inclui o terminador '\0'
    close(fd);
}

int main(int argc, char *argv[]) {
    printf("Ola. A enviar comando de teste...\n");
    enviar_comando("AGENDAR 12 5km LISBOA\n");
    return 0;
}
