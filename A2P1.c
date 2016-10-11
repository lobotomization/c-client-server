//TEST TEST THIS IS A TEST
//THIS IS ANOTHER TEST
//Test three
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>       
#include <fcntl.h>     
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#define NUMCHLD 10
#define opensharedmem(shmname, shmfd, type, arrayname, length){\
	(shmfd) = shm_open((shmname), O_RDWR|O_CREAT, 0666);\
	ftruncate((shmfd), sizeof(type)*(length));\
	(arrayname) = (type *)mmap(0, sizeof(type)*(length), PROT_READ | PROT_WRITE, MAP_SHARED, (shmfd), 0);\
}\
while(0)

#define initsharedmem(shmname, shmfd, type, arrayname, length){\
	opensharedmem(shmname, shmfd, type, arrayname, length);\
	memset((arrayname), 0, sizeof(type)*(length));\
}\
while(0)

#define closesharedmem(shmname, shmfd, type, arrayname, length){\
	munmap((arrayname), (length)*sizeof(type));\
	shm_unlink(shmname);\
	close(shmfd);\
}\
while(0)

struct sockaddr_in getSockaddrFromPort(int port);
struct prog_stats{
	int size;
	int compexitstatus;
	char *sourcename;
	char *execname;
	int progexitstatus;
	time_t received;
	time_t written;
	time_t compiled;
	time_t started;
	time_t finished;
	time_t runtime;
	time_t replytime;
	time_t deleted;
};

int spawnChildren(int num);
int spawnChild();
void handleConnection(int connection, int port);
int listenOnPort(int INITPORT, int *listener);
char * stringToUpper(char *str, int n);
char * stringToLower(char *str, int n);
FILE * fileFromPort(int port, char *outName);
int writeLongToString(char *str, long l, int offset);
void printMetadata(int fd, struct prog_stats *metadata);

int main(int argc, char **argv){

	//const int NUMCHLD = 10;
	int pid = 0, index = 0; 
	int shmfd;
	struct prog_stats *childstatus = NULL;
	pthread_mutex_t lock;
	pthread_mutex_init(&lock, NULL);
	/*void reaper(int signal){
		wait(NULL);
		children--;
	}
	if(SIG_ERR == signal(SIGCHLD, reaper))
	{
		perror("Registering zombie reaper failed");
		return EXIT_FAILURE;
	}*/


	/*PIDshmfd = shm_open("PIDTable", O_RDWR|O_CREAT, 0666);
	ftruncate(PIDshmfd, sizeof(int)*(NUMCHLD + 1));
	PIDTable = (int *)mmap(0, sizeof(int)*(NUMCHLD + 1), PROT_READ | PROT_WRITE, MAP_SHARED, PIDshmfd, 0);
	memset(PIDTable, 0, sizeof(int)*(NUMCHLD + 1));*/

	int *PIDTable;
	int PIDshmfd;
	initsharedmem("PIDTable", PIDshmfd, int, PIDTable, NUMCHLD); //This line makes an area of shared memory whose name is "PIDTable", with type int and length NUMCHLD
																	 //The pointer to shared memory is stored in the PIDTable variable
	pthread_mutex_t *lockmem;
	int lockshmfd;
	initsharedmem("lock", lockshmfd, pthread_mutex_t, lockmem, 1);
	*lockmem = lock; //This should set the shared memory to contain the address of the parent's mutex lock
	spawnChildren(NUMCHLD);
	while(1){
		if(-1 == (pid = wait(NULL))){
			perror("Child exited crappily");
		}
		printf("Child exited with PID: %d\n", (int)pid);
		for(index = 0; index < NUMCHLD; index++){
			printf("PID[%d] = %d\n", index, *(PIDTable + index));
		}
		//while(*PIDTable == 1){
		//	sleep(1);
		//}
		//if(*PIDTable == 0)
		//*PIDTable = 1;

		pthread_mutex_lock(&lock);
		for(index = 0; *(PIDTable + index) != (int)pid && index < NUMCHLD; index++); //Calculate index
		printf("Deleting index %d\n", index); 
		*(PIDTable + index) = 0;
		//*PIDTable = 0;
		//printf("Lock status: %d\n", *PIDTable);

		/*shmfd = shm_open("metadata", O_RDWR|O_CREAT, 0666);
		ftruncate(shmfd, sizeof(struct prog_stats)); 
		shm = mmap(0,sizeof(struct prog_stats), PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
		childstatus = (struct prog_stats *)shm;*/
		opensharedmem("metadata", shmfd, struct prog_stats, childstatus, 1);
		printMetadata(1, childstatus);
		closesharedmem("metadata", shmfd, struct prog_stats, childstatus, 1);
		pthread_mutex_unlock(&lock);

		/*munmap(shm, sizeof(struct prog_stats));
		shm_unlink("metadata");
		close(shmfd);*/

		spawnChildren(1);
	}

}



int spawnChildren(int num){
	int children = 0, pid = 0;


	//pthread_mutex_init(&lock, NULL);
	while(children < num)
	{
		printf("Parent forking a child process...\n");			
		pid = fork();
		if(0 == pid)
		{
			/*pthread_mutex_t *lockmem;
			int lockshmfd;
			opensharedmem("lock", lockshmfd, pthread_mutex_t, lockmem, 1);
			pthread_mutex_t lock = *lockmem;*/
			struct flock f_lock;
			f_lock.l_type = F_WRLCK;
			f_lock.l_whence = SEEK_SET;
			f_lock.l_start = 0;
			f_lock.l_len = sizeof(int)*(NUMCHLD + 1);

			int PIDshmfd = shm_open("PIDTable", O_RDWR|O_CREAT, 0666), index = 0;
			ftruncate(PIDshmfd, sizeof(int)*(NUMCHLD + 1));
			int *PIDTable = (int *)mmap(0, sizeof(int)*(NUMCHLD + 1), PROT_READ | PROT_WRITE, MAP_SHARED, PIDshmfd, 0);
			/*while(*PIDTable == 1){   //Be super careful about this, race conditions might happen here or in the next block!
				sleep(1);
			}
			if(*PIDTable == 0){				///This is where proper locking needs to happen, race conditions can still happen here :(
				*PIDTable = 1;*/
				//pthread_mutex_lock(&lock);
				fcntl(PIDshmfd, F_SETLKW, &f_lock);
				for(index = 0; *(PIDTable + index) != 0 && index < NUMCHLD; index++); //Calculate index
				printf("Now adding PID %d to index %d\n", (int)getpid(), index);
				*(PIDTable + index) = (int)getpid();
				f_lock.l_type = F_UNLCK;
				fcntl(PIDshmfd, F_SETLK, &f_lock);
				//pthread_mutex_unlock(&lock);
				//*PIDTable = 0;
				//}
			spawnChild();
			return (int)getpid();
		}
		else if(0 > pid){
			perror("Fork failed");
			return EXIT_FAILURE;
		}
		children++; 
	}
	return EXIT_SUCCESS;
}

int spawnChild(){
	const int INITPORT = 10000;
	int listener, connection, port;
	printf("Child process spawned...\n");
	if(-1 == (port = listenOnPort(INITPORT, &listener))) //Port # is picked here
	{
		return EXIT_FAILURE;	
	}
	connection = accept(listener, NULL, NULL);
	printf("Child process connection now beginning...\n");
	if(0 > connection)
	{
		perror("Accepting connection failed, child exiting...");
		return EXIT_FAILURE;				
	}
	handleConnection(connection, port);
	printf("Child process connection now exiting...\n");
		if(-1 == shutdown(connection, SHUT_RDWR))
	{
		perror("Shutdown failed");
		close(connection);
		close(listener);
		return EXIT_FAILURE;
	}
	close(connection);
	close(listener);
	return EXIT_SUCCESS;
}

void handleConnection(int connection, int port){



	//long curTime = (long)time(NULL);
	fcntl(connection, F_SETFD, FD_CLOEXEC);
	struct prog_stats *metadata = (struct prog_stats *)malloc(sizeof(struct prog_stats));
	memset(metadata, 0, sizeof(struct prog_stats)); //Zero out the struct, in case the user exits immediately
	FILE *outFile = NULL;
	int bytesRead, writingCode = 0, pid = 0, returnCode = 0, size = 0;
	char text[1024] = {0}, sourcename[100] = {0}, execname[100] = {0};
	const char start[] = "start", end[] = "end", stop[] = "stop";
	const char welcome[] = "Welcome to Ian's C auto-compiling server security nightmare!\nType \"%s\" to begin coding and \"%s\" to submit your code and \"%s\" to stop\n";
	dprintf(connection, welcome, start, end, stop);
	bytesRead = read(connection, text, 1024);
	while(-1 != bytesRead)
	{


		if(0 == strncmp(text, stop, 4)){
			if(1 == writingCode){
				fclose(outFile);
			}
			return;
		}
		if(1 == writingCode && 0 != strncmp(text, end, 3)){
			fwrite(text, sizeof(char), bytesRead, outFile); 
			size += bytesRead;				
		}

		if(1 == writingCode && 0 == strncmp(text, end, 3)){
			fclose(outFile);
			metadata->written = time(NULL);
			write(connection, "Code written to file\n", sizeof("Code written to file\n"));
			strncpy(execname, sourcename, strlen(sourcename));
			metadata->size = size;
			metadata->sourcename = sourcename;
			metadata->execname = execname;			
			char *argv[10] = {0};
			argv[0] = "gcc";
			argv[1] = sourcename;
			printf("Source code filename is: %s\n", sourcename);
			argv[2] = "-o";
			*(execname + strlen(execname) - 2) = '\0'; //Chop off .c extension
			argv[3] = execname;
			printf("Executable filename is: %s\n", execname);
			argv[4] = NULL;
			writingCode = 0;
			write(connection, "Now compiling code...\n", sizeof("Now compiling code...\n"));
			pid = fork();
			if(pid == 0){
				dup2(connection, STDOUT_FILENO);//Redirect stderr and stdout to the client's connection
				dup2(connection, STDERR_FILENO);
				execvp(argv[0], argv); 		//Run gcc as a child of the child
			}
			wait(&returnCode);
			metadata->compiled = time(NULL);
			metadata->compexitstatus = returnCode;
			dprintf(connection, "Your return code is: %d\n", returnCode);
			pid = fork();
			metadata->started = time(NULL);
			if(returnCode == 0 && pid == 0){
				dup2(connection, STDOUT_FILENO);//Redirect stderr and stdout to the client's connection
				dup2(connection, STDERR_FILENO);
				argv[0] = execname;
				argv[1] = NULL;
				write(connection, "Now executing code...\n", sizeof("Now executing code...\n"));
				execvp(argv[0], argv); //Requires . to be in PATH variable to work
				//This executes the compiled code
			}
			wait(&returnCode);
			metadata->finished = time(NULL);
			metadata->runtime = metadata->finished - metadata->started;
			metadata->progexitstatus = returnCode;
			unlink(sourcename);
			unlink(execname);
			metadata->deleted = time(NULL);
			if(0 == metadata->compexitstatus){
				dprintf(connection, "Finished executing with code %d\n", returnCode);	
			}
			else
			{
				dprintf(connection, "Compilation failed\n");
			}
			metadata->replytime = time(NULL);
			int shmfd;
			struct prog_stats *shmprogstats;
			opensharedmem("metadata", shmfd, struct prog_stats, shmprogstats, 1);
			/*
			int shmfd = shm_open("metadata", O_RDWR|O_CREAT, 0666);
			ftruncate(shmfd, sizeof(struct prog_stats)); //I should figure out a better bound than just 4KB for no good reason
			void *shm = mmap(0,sizeof(struct prog_stats), PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);  //Link shared memory
			*/
			memcpy((void *)shmprogstats, (void *)metadata, sizeof(struct prog_stats));

			//struct prog_stats *shmprogstats = (struct prog_stats *)shm; //Cast the void shm pointer to a struct type to access fields
			shmprogstats -> sourcename = (char *)malloc(strlen(metadata->sourcename));
			strncpy(shmprogstats -> sourcename, metadata->sourcename, strlen(metadata->sourcename) + 1); 
			//These strncpy and malloc lines make sure that shm has a copy of the data being pointed to
			//Rather than just a copy of the pointers
			shmprogstats -> execname = malloc(strlen(metadata->execname));
			strncpy(shmprogstats -> execname, metadata->execname, strlen(metadata->execname) + 1);
			printMetadata(connection, shmprogstats);

			//munmap(shm, sizeof(struct prog_stats));
			//shm_unlink("metadata");
			close(shmfd);
			
			//printMetadata(connection, metadata);
		}

		if(0 == writingCode && 0 == strncmp(text, start, 5)){
			outFile = fileFromPort(port, sourcename);	
			printf("Filename received: %s\n", sourcename);		
			if(NULL == outFile){
				perror("Failed to create file");
			}
			else{
				write(connection, "Now writing code to file\n", sizeof("Now writing code to file\n"));
				writingCode = 1;
				metadata->received = time(NULL);
			}
		}
		bytesRead = read(connection, text, 1024);
	}
}

struct sockaddr_in getSockaddrFromPort(int port){
	struct sockaddr_in out;
	memset(&out, 0, sizeof(out));
	out.sin_port = htons(port);
	//inet_aton(IP, (struct in_addr *)&out.sin_addr.s_addr);
	out.sin_addr.s_addr = htonl(INADDR_ANY);
	out.sin_family = AF_INET;
	return out;
}

int listenOnPort(int INITPORT, int *listener){
	int portCounter = 0;
	struct sockaddr_in servProps = getSockaddrFromPort(INITPORT + portCounter);
	if(-1 == (*listener = socket(AF_INET, SOCK_STREAM, 0)))
	{
		perror("Creating socket failed");
		return -1;
	}
	printf("Created socket\n");
	for (portCounter = 1; -1 == bind(*listener, (struct sockaddr *)&servProps, sizeof(servProps)); portCounter++)
	{
		fprintf(stderr, "Bind failed on port %d\n", INITPORT + portCounter - 1);
		servProps = getSockaddrFromPort(INITPORT + portCounter);
	}
	printf("Bound socket to port %d\n", INITPORT + portCounter - 1);
	if(-1 == listen(*listener, 1)) //Listen for at most 1 connection
	{
		perror("Listening failed");
		return -1;
	}
	printf("Now listening on socket\n");
	return INITPORT + portCounter - 1;
}

char * stringToUpper(char *str, int n){ //Makes the first n chars of a string uppercase
	const char *out = str;
	while(*str != '\0'&& n > 0){
		*str = toupper(*str);
		str++;
		n--;
	}
	return (char *)out;
}

char * stringToLower(char *str, int n){ //Makes the first n chars of a string lowercase
	const char *out = str;
	while(*str != '\0'&& n > 0){
		*str = tolower(*str);
		str++;
		n--;
	}
	return (char *)out;
}


FILE * fileFromPort(int port, char *outName){
	int offset = 0;
 	char filename[100] = {0};
	offset += writeLongToString(filename, port, offset);
	filename[offset] = '-';
	offset++;
	offset += writeLongToString(filename, (long)time(NULL), offset);
	strncpy(filename+offset, ".c\0", 3);	
	offset += 3;
	printf("Trying to create file with name %s\n", filename);
	strncpy(outName, filename, offset);
	return fopen(filename, "w+");
}
int writeLongToString(char *str, long l, int offset){
	const int longSize = snprintf(NULL, 0, "%ld", l);
	snprintf(str+offset, longSize+1, "%ld", l);
	return (int)longSize;
}

void printMetadata(int fd, struct prog_stats *metadata){
	dprintf(fd, "*****************************************PROGRAM METADATA*****************************************\n");
	dprintf(fd, "Program size: %d\nCompiler exit status: %d\nSource code name: %s\nExecutable name: %s\n",
	metadata->size, metadata->compexitstatus, metadata->sourcename, metadata->execname);
	if(metadata->compexitstatus == 0){
		dprintf(fd, "Program exit status: %d\nTime when execution began: %lu\nTime when execution finished: %lu\nRun time: %lu\n", 
		metadata->progexitstatus, metadata->started, metadata->finished, metadata->runtime);
	}
	dprintf(fd, "Time received: %lu\nTime when written to disk: %lu\nTime when compiled: %lu\nTime when reply was made: %lu\nTime when files were deleted: %lu\n",
	metadata->received, metadata->written, metadata->compiled, metadata->replytime, metadata->deleted);
	dprintf(fd, "**************************************************************************************************\n");
	
}

