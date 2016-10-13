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

	if(argc == 2){
		initPort = strtol(argv[1], NULL, 10);

	}
	if(argc >= 3){
		serverAddress = argv[1];
		initPort = strtol(argv[2], NULL, 10);
		if(argc > 3){
			printf("Too many arguments, extras discarded.");
		}
	}

	if(-1 == connectOnPort(initPort, serverAddress, &socket)){
		fprintf(stderr, "Connecting failed!\nAre you sure the client is available?\n");
		return -1;
	}

	pthread_t readID, writeID;
	pthread_create(&readID, NULL, &readThread, &socket);
	pthread_create(&writeID, NULL, &writeThread, &socket);

	void intHandler(int sig){
		write(socket, "end", 3);
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
		bytes = read(socket, buffer, BUFFSIZE);	//Read from socket
		/*if(strncmp(buffer, "done", 4) == 0){
			kill(0, SIGIO);
		}
		else{*/
			write(1, buffer, bytes); 				//Write to stdout
		//}
	}
	return NULL;
}

void *writeThread(void *arg){

	/*void ioHandler(int sig){
		//write(socket, "end", 3);
		//pthread_cancel(readID);
		//pthread_cancel(writeID);
	}
	if(SIG_ERR == signal(SIGIO, ioHandler)){
		perror("Error registering I/O handler");
	}

	sigset_t mask, oldmask;
	sigfillset(&mask);
	sigdelset(&mask, SIGIO);*/

	char buffer[BUFFSIZE], tempbuffer[BUFFSIZE];
	int socket = *(int *)arg, bytes = 1, fp = 0;
	while(1){

		//write(1, "C Interpreter> ", sizeof("C Interpreter> "));  //This will look good once I get reading and writing from/to the client and server more nicely synced up
		bytes = read(0, buffer, BUFFSIZE);	//Read from stdin



		if(strncmp(buffer, "upload", 6) == 0){					//Crazy upload handling block starts here. Upload files using the client as follows: upload file1.c file2.c file3.c ...
			//sigprocmask(SIG_SETMASK, &mask, &oldmask);
			strncpy(tempbuffer, buffer, bytes);					//Copy the contents of the buffer (which contains the upload command) over to a tempbuffer since we will be reusing the buffer to read from the file
			tempbuffer[bytes] = '\0';							//Make sure to delimit the tempbuffer
			//printf("Contents of tempbuffer: %s\n", tempbuffer);
			strtok(tempbuffer, " \n");							//Tokenize out the word upload
			char *filename = strtok(NULL, " \n");				//Get the first filename
			while(filename!=NULL){
				fp = open(filename, O_RDONLY);
				if(fp != -1){
					write(socket, "start", 5);
					//sigsuspend(&mask);
					sleep(1);									//I really need to wait for server response here, rather than just sleep for a second
					/*struct timespec waittime;
					waittime.tv_sec = 0;
					waittime.tv_nsec = 100000;
					nanosleep(&waittime, NULL);*/
					bytes = read(fp, buffer, BUFFSIZE);
					while(bytes != 0){
						write(1, buffer, bytes);				//This displays the c code on stdout, disable for nicer upload output
						write(socket, buffer, bytes);
						bytes = read(fp, buffer, BUFFSIZE);
					}
					write(1, "\n", 1); //Print a new line, in case there isn't one printed by the final write command of the while loop
					write(socket, "\n", 1); //Print a new line, in case there isn't one printed by the final write command of the while loop
					sleep(1);						//Waiting for a second here should be okay... I really need the writes to finish on the server-side before writing stop

					//nanosleep(&waittime, NULL);
					write(1, "stop\n", 5);
					write(socket, "stop\n", 5);
					close(fp);
				}
				else{
					perror("File couldn't be opened");
					fprintf(stderr, "File %s couldn't be opened\n", filename);
				}
				filename = strtok(NULL, " \n");
				sleep(1);										//I really need to wait for server response here, rather than just sleep for a second
			}
		}														//Crazy upload handling block ends here



		write(socket, buffer, bytes);		//Write to socket
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
		if(99 == portCounter){  //Bail out on hundredth port
			return -1;
		}
	}
	printf("Connected socket to port %d\n", INITPORT + portCounter - 1);
	return INITPORT + portCounter - 1;
}