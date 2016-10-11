#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#define BUFFSIZE 1024
struct sockaddr_in getSockaddrFromPortAndServer(int port, char *server);
int connectOnPort(int INITPORT, char *server, int *listener);
void *readThread(void *arg);
void *writeThread(void *arg);

int main(int argc, char* argv[]){
	char * serverAddress = "127.0.0.1";
	int initPort = 10000, socket;

	if(argc > 1){
		serverAddress = argv[1];
		if(argc > 2){
			initPort = strtol(argv[2], NULL, 10);
		}
		if(argc > 3){
			printf("Too many arguments, discarded.");
		}
	}
	connectOnPort(initPort, serverAddress, &socket);

	/*bytes = read(socket, buffer, BUFFSIZE);
	write(1, buffer, bytes); fflush(stdout);
	while(1){
		printf("Arg>"); fflush(stdout);
		bytes = read(0, buffer, BUFFSIZE);
		write(socket, buffer, bytes);
		printf("Now reading from server\n");
		if((bytes = recv(socket, buffer, BUFFSIZE, 0)));
		write(1, buffer, bytes); fflush(stdout);
	}*/
	pthread_t readID, writeID;
	pthread_create(&readID, NULL, &readThread, &socket);
	pthread_create(&writeID, NULL, &writeThread, &socket);

	void intHandler(int sig){
		write(socket, "stop", 4);
		pthread_cancel(readID);
		pthread_cancel(writeID);
	}
	if(SIG_ERR == signal(SIGINT, intHandler)){
		perror("Error registering interupt handler");
	}
	
	pthread_join(readID, NULL); //Only wait until server-read thread is dead to exit, no need to wait for server-write thread
	//pthread_join(writeID, NULL);
	return 0;
}

void *readThread(void *arg){
	char buffer[BUFFSIZE];
	int socket = *(int *)arg, bytes = 1;
	while(bytes != 0){
		bytes = read(socket, buffer, BUFFSIZE);
		//printf("Now reading from server, %d bytes\n", bytes);
		write(1, buffer, bytes); //fflush(stdout);
	}
	return NULL;
}

void *writeThread(void *arg){
	char buffer[BUFFSIZE];
	int socket = *(int *)arg, bytes = 1;
	while(1){
		//printf("Arg>"); fflush(stdout);
		bytes = read(0, buffer, BUFFSIZE);
		write(socket, buffer, bytes);
		/*if(strncmp(buffer, "stop", 4) == 0){
			break;
		}*/
	}
	return NULL;
}

struct sockaddr_in getSockaddrFromPortAndServer(int port, char *server){
	struct sockaddr_in out;
	memset(&out, 0, sizeof(out));
	out.sin_port = htons(port);
	//inet_aton(IP, (struct in_addr *)&out.sin_addr.s_addr);
	//out.sin_addr.s_addr = htonl(INADDR_ANY);
	inet_aton(server, (struct in_addr *)&out.sin_addr.s_addr);
	out.sin_family = AF_INET;
	return out;
}

int connectOnPort(int INITPORT, char *server, int *listener){
	int portCounter = 0;
	struct sockaddr_in servProps = getSockaddrFromPortAndServer(INITPORT + portCounter, server);
	if(-1 == (*listener = socket(AF_INET, SOCK_STREAM, 0)))
	{
		perror("Creating socket failed");
		return -1;
	}
	//fcntl(*listener, F_SETFL, O_NONBLOCK);
	printf("Created socket\n");
	for (portCounter = 1; 0 != connect(*listener, (struct sockaddr *)&servProps, sizeof(servProps)); portCounter++)
	{
		fprintf(stderr, "Connect failed on port %d\n", INITPORT + portCounter - 1);
		servProps = getSockaddrFromPortAndServer(INITPORT + portCounter, server);
	}
	printf("Connected socket to port %d\n", INITPORT + portCounter - 1);
	return INITPORT + portCounter - 1;
}