CC = gcc
CFLAGS = -Wall -g
TARGETS = controlador cliente veiculo

all: $(TARGETS)

controlador: controlador.o
	$(CC) $(CFLAGS) -o controlador controlador.o

cliente: cliente.o
	$(CC) $(CFLAGS) -o cliente cliente.o

veiculo: veiculo.o
	$(CC) $(CFLAGS) -o veiculo veiculo.o

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGETS) *.o