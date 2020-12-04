#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include "SerialManager.h"

#define SERIAL_BAUD_RATE 115200
#define SERIAL_PORT_NUMBER 1 
#define SW_MSG_LENGTH 9
#define SERIAL_MSG_LENGTH 10
#define CONNECTION_PORT 10000
#define CONNECTION_IP "127.0.0.1"

volatile sig_atomic_t got_sigint, got_sigterm;
int socket_on; //Variable de estado de la conexion del cliente
typedef void (*_sig_func_ptr)(int);

pthread_mutex_t mutexSerial = PTHREAD_MUTEX_INITIALIZER;

// Funcion que atiende la conexión entrante con el read bloqueante
void *listenToSocket(void *infd)
{
	int fd = *((int *)infd);
	printf("Abri un thread nuevo\n");
	char buffer[128];
	int n = 0;

	// Chequeo que no haya habido error de lectura
	while (n != -1)
	{
		// Leemos mensaje de cliente
		if ((n = read(fd, buffer, 128)) == -1)
		{
			perror("Error leyendo mensaje en socket");
		}
		buffer[n] = 0;

		if (n != 0)
		{
			printf("From interface... %d bytes.:%s\n", n, buffer);
			//ENVIAR A SERIE
			pthread_mutex_lock(&mutexSerial);
			serial_send(buffer, SERIAL_MSG_LENGTH);
			pthread_mutex_unlock(&mutexSerial);
		}
		else
		{
			perror("Buffer");
			break;
		};
		
	}

	printf("Cierro thread\n");
	socket_on = 0;
	close(fd);
	pthread_exit(NULL);
}

void bloquearSign(void)
{
    sigset_t set;
    int s;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void desbloquearSign(void)
{
    sigset_t set;
    int s;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

void setSignalHandler(int sig, _sig_func_ptr handler, int flags, char *errorStr)
{
	struct sigaction sa;

	sa.sa_handler = handler;
	sa.sa_flags = flags;
	sigemptyset(&sa.sa_mask);

	if (sigaction(sig, &sa, NULL) == -1)
	{
		perror(errorStr);
		exit(1);
	}
}

void handlerSIGINT(int sig)
{
	got_sigint = 1;
}

void handlerSIGTERM(int sig)
{
	got_sigterm = 1;
}

int createSocket(){

	struct sockaddr_in serveraddr;
	// Creamos socket
	int s = socket(PF_INET, SOCK_STREAM, 0);

	// Cargamos datos de IP:PORT del server
	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(CONNECTION_PORT);
	serveraddr.sin_addr.s_addr = inet_addr(CONNECTION_IP);
	if (serveraddr.sin_addr.s_addr == INADDR_NONE)
	{
		fprintf(stderr, "ERROR invalid server IP\r\n");
		return -1;
	}

	// Abrimos puerto con bind()
	if (bind(s, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)
	{
		close(s);
		perror("Listener: bind");
		return -1;
	}

	// Seteamos socket en modo Listening
	if (listen(s, 10) == -1) // backlog=10
	{
		close(s);
		perror("Error en listen");
		return -1;
	}

	return s;

}

int main()
{
	socklen_t addr_len;
	struct sockaddr_in clientaddr;
	char buffer[128];
	int s,newfd,n;
	pthread_t client;

	got_sigint = 0;
	got_sigterm = 0;
	addr_len = sizeof(struct sockaddr_in);

	//Asigno handlers de señales
	setSignalHandler(SIGINT, handlerSIGINT, 0, "handlerSIGINT");
	setSignalHandler(SIGINT, handlerSIGTERM, 0, "handlerSIGTERM");

	//Inicio puerto serie
	serial_open(SERIAL_PORT_NUMBER, SERIAL_BAUD_RATE);

	//Creo y configuro socket
	if ((s = createSocket()) == -1){
		printf("Error createSocket\n");
		exit(EXIT_FAILURE);
	}
	
	//Mientras no reciba señales
	while ((got_sigint == 0) && (got_sigterm == 0))
	{

		// Ejecutamos accept() para recibir conexiones entrantes
		if ((newfd = accept(s, (struct sockaddr *)&clientaddr, &addr_len)) == -1)
		{
			perror("Error en accept");
			exit(EXIT_FAILURE);
		}
		
		printf("server:  conexion desde:  %s\n", inet_ntoa(clientaddr.sin_addr));
		socket_on = 1;

		//Creo thread para atender al cliente
		bloquearSign();
		if (pthread_create(&client, NULL, listenToSocket, &newfd))
		{
			perror("Error al crear thread para atender al cliente");
			exit(EXIT_FAILURE);
		}
		desbloquearSign();

		// Escucho periodicamente el puerto serie
		while ((socket_on == 1) && (got_sigint == 0) && (got_sigterm == 0))
		{
			pthread_mutex_lock(&mutexSerial);
			if ((n =serial_receive(buffer, 128)) != 0)
			{
				buffer[n] = '\0';
				printf("From serial... %d bytes: %s\n",n, buffer);
				if (write(newfd, buffer, SW_MSG_LENGTH) == -1)
				{
					perror("Error escribiendo mensaje en socket");
					exit(EXIT_FAILURE);
				}
			}
			pthread_mutex_unlock(&mutexSerial);
			sleep(1);
		}

	}

	printf("Cerrando programa\n");
	pthread_cancel(client);
	exit(EXIT_SUCCESS);

	return 0;
}
