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
#include <errno.h>
//#include <pthread.h>
#define NUMCHLD 10
#define TIMEOUT 900 //900 seconds = 15 minute timeout
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

struct status{
	int statusID;
	time_t start;
};
void killServerGracefully(int signal);
char *statusPrinter(int status);
void status(int signal);
struct sockaddr_in getSockaddrFromPort(int port); 
void compileCode(char *sourcename, char *execname, int connection);
void executeCode(char *execname, int connection);
int spawnChildren(int num);
int spawnChild();
void handleConnection(int listener, int connection, int port);
int listenOnPort(int INITPORT, int *listener);
char * stringToUpper(char *str, int n);
char * stringToLower(char *str, int n);
FILE * fileFromPort(int port, char *outName);
int writeLongToString(char *str, long l, int offset);
void printMetadata(int fd, struct prog_stats *metadata);

int main(int argc, char **argv){

	int pid = 0, index = 0; 
	int shmfd;
	struct prog_stats *childstatus = NULL;
	initsharedmem("metadata", shmfd, struct prog_stats, childstatus, 1);
	/*void reaper(int signal){
		wait(NULL);
		children--;
	}
	if(SIG_ERR == signal(SIGCHLD, reaper))
	{
		perror("Registering zombie reaper failed");
		return EXIT_FAILURE;
	}*/
	int statusshmfd;												//initialize the statuses to zero
	struct status *statuses;
	initsharedmem("status", statusshmfd, struct status, statuses, NUMCHLD);

	if(SIG_ERR == signal(SIGUSR1, status))
	{
		perror("Registering status printer failed");
		return EXIT_FAILURE;
	}

	if(SIG_ERR == signal(SIGUSR2, killServerGracefully))
	{
		perror("Registering graceful server ending signal (SIGUSR2) failed");
		return EXIT_FAILURE;
	}
	int *PIDTable;
	int PIDshmfd;
	initsharedmem("PIDTable", PIDshmfd, int, PIDTable, NUMCHLD); //This line makes an area of shared memory whose name is "PIDTable", with type int and length NUMCHLD
																	 //The pointer to shared memory is stored in the PIDTable variable
	struct flock pid_lock, meta_lock;

	pid_lock.l_type = F_WRLCK;
	pid_lock.l_whence = SEEK_SET;
	pid_lock.l_start = 0;
	pid_lock.l_len = sizeof(int)*(NUMCHLD + 1);

	meta_lock.l_type = F_WRLCK;
	meta_lock.l_whence = SEEK_SET;
	meta_lock.l_start = 0;
	meta_lock.l_len = sizeof(struct prog_stats);

	spawnChildren(NUMCHLD);
	pid = fork();
	if(pid > 0){ //Do parent stuff
		while(1){
			if(-1 == (pid = wait(NULL))){
				perror("Child exited crappily");
			}

			opensharedmem("metadata", shmfd, struct prog_stats, childstatus, 1);
			fcntl(shmfd, F_SETLKW, &meta_lock);

			printf("Child exited with PID: %d\n", (int)pid);
			for(index = 0; index < NUMCHLD; index++){
				printf("PID[%d] = %d\n", index, *(PIDTable + index));
			}


			fcntl(PIDshmfd, F_SETLKW, &pid_lock);
			for(index = 0; *(PIDTable + index) != (int)pid && (statuses + index)->statusID != 14 && index < NUMCHLD; index++); //Calculate index
			printf("Deleting index %d\n", index); 
			*(PIDTable + index) = 0;
			pid_lock.l_type = F_UNLCK;
			fcntl(PIDshmfd, F_SETLK, &pid_lock);


			//opensharedmem("metadata", shmfd, struct prog_stats, childstatus, 1);
			printMetadata(1, childstatus);
			closesharedmem("metadata", shmfd, struct prog_stats, childstatus, 1);
			meta_lock.l_type = F_UNLCK;
			fcntl(shmfd, F_SETLK, &meta_lock);


			spawnChildren(1);
		}
	}else if(pid == 0){			//This child process periodically scans for child processes which are taking too long in any state other than listening i.e. the default state
		while(1){
			sleep(1);
			time_t curTime = time(NULL);
			for(index = 0; index < NUMCHLD; index++){
				if((statuses + index)->start > 0 && curTime - (statuses + index)->start > TIMEOUT && (statuses + index)->statusID != 2){
					printf("Now killing PID %d\n", *(PIDTable + index));
					kill(*(PIDTable + index), SIGTERM);
				}
			}
		}
	}

}
void killServerGracefully(int signal){
	kill(0, SIGTERM);
}
void status(int signal){
	int statusshmfd;
	struct status *statuses;
	opensharedmem("status", statusshmfd, struct status, statuses, NUMCHLD);
	int *PIDTable;
	int PIDshmfd;
	opensharedmem("PIDTable", PIDshmfd, int, PIDTable, NUMCHLD);
	printf("Index number\tPID\tStatus\t\t\tTime in State\n");
	for(int i = 0; i < NUMCHLD; i++){
		printf("%d\t\t%d\t%s\t\t\t%lu\n", i, *(PIDTable + i), statusPrinter((statuses+i)->statusID), time(NULL) - (statuses+i)->start);
		//printf("*(statuses + %d) = %p\n", i, *(statuses + i));
		//printf("*(statuses + %d) points to the string %s\n", i, *(statuses + i));
	}

}
char *statusPrinter(int status){
	switch(status){
		case 0:
		return "Unstarted";
		case 1:
		return "Initializing";
		case 2:
		return "Listening";
		case 3:
		return "Connecting";
		case 4:
		return "Connected";
		case 5:
		return "Connected and reading"; //Children appear to be getting stuck in this state when the client is closed abruptly...
		case 6:
		return "Connected and writing code";
		case 7:
		return "Connected and stopping coding";
		case 8:
		return "Connected and compiling code";
		case 9:
		return "Connected and executing code";
		case 10:
		return "Connected and writing metadata";
		case 11:
		return "Ending session";
		case 12:
		return "Exiting";
		case 13:
		return "Connection closed";
		case 14:
		return "Child process completed"; //This state is used to determine where to place new PIDs in the PIDtable
		default:
		return "Unknown state";
	}
}
int spawnChildren(int num){
	int children = 0, pid = 0, returnCode = 0;


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

			int statusshmfd;
			struct status *statuses;
			opensharedmem("status", statusshmfd, struct status, statuses, NUMCHLD);

			int *PIDTable;
			int PIDshmfd , index;
			opensharedmem("PIDTable", PIDshmfd, int, PIDTable, NUMCHLD);

			fcntl(PIDshmfd, F_SETLKW, &f_lock);
			for(index = 0; *(PIDTable + index) != 0 && index < NUMCHLD; index++); //Calculate index
			printf("Now adding PID %d to index %d\n", (int)getpid(), index);
			*(PIDTable + index) = (int)getpid();
			f_lock.l_type = F_UNLCK;
			fcntl(PIDshmfd, F_SETLK, &f_lock);
			(statuses + index)->statusID = 1; //Status 1: Initializing
			(statuses + index)->start = time(NULL);

			spawnChild();
			(statuses + index)->statusID = 14; //Status 14: Child process completed
			(statuses + index)->start = time(NULL);


			return (int)getpid();
		}
		else if(0 > pid){
			perror("Fork failed");
			return EXIT_FAILURE;
		}
		children++; 
	}
	/*while(children--){
	wait(&returnCode);
	printf("From spawnChildren: Child exited with return code: %d\n", returnCode);
	}*/
	return EXIT_SUCCESS;
}

int spawnChild(){
	const int INITPORT = 10000;
	int listener, connection, port;

	int statusshmfd;
	struct status *statuses;
	opensharedmem("status", statusshmfd, struct status, statuses, NUMCHLD);

	int *PIDTable;
	int PIDshmfd , index;
	opensharedmem("PIDTable", PIDshmfd, int, PIDTable, NUMCHLD);
	for(index = 0; *(PIDTable + index) != getpid() && index < NUMCHLD; index++);
	//printf("%s\n", *(statuses + index));

	printf("Child process spawned...\n");
	if(-1 == (port = listenOnPort(INITPORT, &listener))) //Port # is picked here
	{
		return EXIT_FAILURE;	
	}
	
	(statuses + index)->statusID = 2; // Status 2: Listening 
	(statuses + index)->start = time(NULL);

	connection = accept(listener, NULL, NULL);
	printf("Child process connection now beginning...\n");
	(statuses + index)->statusID = 3; // Status 3: Connecting
	(statuses + index)->start = time(NULL);
	if(0 > connection)
	{
		perror("Accepting connection failed, child exiting...");
		return EXIT_FAILURE;				
	}
	handleConnection(listener, connection, port); 
	(statuses + index)->statusID = 12; // Status 12: Exiting
	(statuses + index)->start = time(NULL);

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
	(statuses + index)->statusID = 13; // Status 13: Connection closed
	(statuses + index)->start = time(NULL);

	return EXIT_SUCCESS;
}

void handleConnection(int listener, int connection, int port){




	int statusshmfd;
	struct status *statuses;
	opensharedmem("status", statusshmfd, struct status, statuses, NUMCHLD);

	int *PIDTable;
	int PIDshmfd , index;
	opensharedmem("PIDTable", PIDshmfd, int, PIDTable, NUMCHLD);
	for(index = 0; *(PIDTable + index) != getpid() && index < NUMCHLD; index++);
	(statuses + index)->statusID = 4; // Status 4: Connected
	(statuses + index)->start = time(NULL);

	struct prog_stats *metadata = (struct prog_stats *)malloc(sizeof(struct prog_stats));
	memset(metadata, 0, sizeof(struct prog_stats)); //Zero out the struct, in case the user exits immediately
	
	FILE *outFile = NULL;
	int bytesRead, writingCode = 0, returnCode = 0, size = 0, dying = 0;
	char text[1024] = {0}, sourcename[100] = {0}, execname[100] = {0};

	const char start[] = "start", end[] = "exit", stop[] = "stop";
	const char welcome[] = "Welcome to Ian's C auto-compiling server security nightmare!\nType \"%s\" to begin coding and \"%s\" to submit your code and \"%s\" to end the session\n";
	
	dprintf(connection, welcome, start, stop, end);
	(statuses + index)->statusID = 5; // Status 5: Connected and reading
	(statuses + index)->start = time(NULL);


	void dieGracefully(int signal){
		if(dying == 0){
			if(outFile != NULL){
				fclose(outFile);
			}
			unlink(sourcename);
			unlink(execname);
		}
		dying = 1;
		
		//write()

		
		(statuses + index)->statusID = 12; // Status 12: Exiting
		(statuses + index)->start = time(NULL);

		printf("Child process connection now exiting...\n");
			if(-1 == shutdown(connection, SHUT_RDWR))
		{
			perror("Shutdown failed");
			//close(connection);
			//close(listener);
			//return EXIT_FAILURE;
		}
		close(connection);
		close(listener);
		(statuses + index)->statusID = 13; // Status 13: Connection closed
		(statuses + index)->start = time(NULL);
		sleep(1);
		kill(getpid(), SIGKILL);
		//return EXIT_SUCCESS;
		//return;
	}
	if(SIG_ERR == signal(SIGTERM, dieGracefully)){
		perror("Error registering SIGTERM handler");
	}

	bytesRead = read(connection, text, 1024);
	while(-1 != bytesRead)
	{

		

		if(1 == dying || 0 == strncmp(text, end, strlen(end))){
			(statuses + index)->statusID = 11; // Status 11: Ending session
			(statuses + index)->start = time(NULL);

			if(1 == writingCode){
				fclose(outFile);
			}
			break;
		}
		if(1 == writingCode && 0 != strncmp(text, stop, strlen(stop))){
			fwrite(text, sizeof(char), bytesRead, outFile); 
			size += bytesRead;				
		}

		if(1 == writingCode && 0 == strncmp(text, stop, strlen(stop))){
			(statuses + index)->statusID = 7; // Status 7: Connected and stopping coding
			(statuses + index)->start = time(NULL);

			writingCode = 0;
			fclose(outFile);
			metadata->written = time(NULL);
			write(connection, "Code written to file\n", sizeof("Code written to file\n"));
			strncpy(execname, sourcename, strlen(sourcename));
			*(execname + strlen(execname) - 2) = '\0'; //Chop off .c extension
			metadata->size = size;
			metadata->sourcename = sourcename;
			metadata->execname = execname;	
			(statuses + index)->statusID = 8; // Status 8: Connected and compiling code
			(statuses + index)->start = time(NULL);

			compileCode(sourcename, execname, connection);

			wait(&returnCode);
			metadata->compiled = time(NULL);
			metadata->compexitstatus = returnCode;
			dprintf(connection, "Your return code is: %d\n", returnCode);	
			metadata->started = time(NULL);
			if(returnCode == 0){
				(statuses + index)->statusID = 9; // Status 9: Connected and executing code
				(statuses + index)->start = time(NULL);
				executeCode(execname, connection);
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

			struct flock meta_lock;
			meta_lock.l_type = F_WRLCK;
			meta_lock.l_whence = SEEK_SET;
			meta_lock.l_start = 0;
			meta_lock.l_len = sizeof(struct prog_stats);

			int shmfd;
			struct prog_stats *shmprogstats;
			opensharedmem("metadata", shmfd, struct prog_stats, shmprogstats, 1);
			fcntl(shmfd, F_SETLKW, &meta_lock);
			(statuses + index)->statusID = 10; // Status 10: Connected and writing metadata
			(statuses + index)->start = time(NULL);

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
			shmprogstats -> execname = (char *)malloc(strlen(metadata->execname));
			strncpy(shmprogstats -> execname, metadata->execname, strlen(metadata->execname) + 1);
			meta_lock.l_type = F_UNLCK;
			fcntl(shmfd, F_SETLK, &meta_lock);
			printMetadata(connection, shmprogstats);

			//munmap(shm, sizeof(struct prog_stats));
			//shm_unlink("metadata");
			close(shmfd);
			(statuses + index)->statusID = 5; // Status 5: Connected and reading
			(statuses + index)->start = time(NULL);

			//printMetadata(connection, metadata);
		}

		if(0 == writingCode && 0 == strncmp(text, start, strlen(start))){
			outFile = fileFromPort(port, sourcename);	
			printf("Filename received: %s\n", sourcename);		
			if(NULL == outFile){
				perror("Failed to create file");
			}
			else{
				write(connection, "Now writing code to file\n", sizeof("Now writing code to file\n"));
				//write(connection, "done", sizeof("done"));

				writingCode = 1;
				metadata->received = time(NULL);
				(statuses + index)->statusID = 6; // Status 6: Connected and writing code
				(statuses + index)->start = time(NULL);

			}

		}
	bytesRead = read(connection, text, 1024);	

	}


}

void executeCode(char *execname, int connection){
	char *argv[2] = {execname, NULL};
	write(connection, "Now executing code...\n", sizeof("Now executing code...\n"));
	int pid = fork();
	if(pid == 0){
		dup2(connection, STDOUT_FILENO);//Redirect stderr and stdout to the client's connection
		dup2(connection, STDERR_FILENO);
		execvp(argv[0], argv); //Requires . to be in PATH variable to work
		//This executes the compiled code
	}
}

void compileCode(char *sourcename, char *execname, int connection){
	char *argv[5] = {"gcc", sourcename, "-o", execname, NULL};
	printf("Source code filename is: %s\n", sourcename);
	printf("Executable filename is: %s\n", execname);
	write(connection, "Now compiling code...\n", sizeof("Now compiling code...\n"));
	int pid = fork();
	if(pid == 0){
		dup2(connection, STDOUT_FILENO);//Redirect stderr and stdout to the client's connection
		dup2(connection, STDERR_FILENO);
		execvp(argv[0], argv); 		//Run gcc as a child of the child
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
	int portCounter = 0, opt = 1;
	struct sockaddr_in servProps = getSockaddrFromPort(INITPORT + portCounter);
	if(-1 == (*listener = socket(AF_INET, SOCK_STREAM, 0)))
	{
		perror("Creating socket failed");
		return -1;
	}
	printf("Created socket\n");
	//setsockopt(*listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); //Set the socket to be reusable immediatel when done with. May potentially lead to a client receiving the end of another client's session...
	//SO_REUSEADDR is a pain to use, it's easier to keep the ports unbindable for a minute or two after completion.
	for (portCounter = 1; -1 == bind(*listener, (struct sockaddr *)&servProps, sizeof(servProps)); portCounter++)
	{
		fprintf(stderr, "Bind failed on port %d\n", INITPORT + portCounter - 1);
		servProps = getSockaddrFromPort(INITPORT + portCounter);
	}
	printf("Bound socket to port %d\n", INITPORT + portCounter - 1);

	if(-1 == listen(*listener, 1)) //Listen for at most 1 connection
	{
		
	/*	if(EADDRINUSE == errno){ //Handle this error more gracefully. It's caused by the setsockopt above allowing children to bind on the same socket, which they sometimes do.
			do{
				perror("Listening failed");
				for (portCounter++; -1 == bind(*listener, (struct sockaddr *)&servProps, sizeof(servProps)) && portCounter < 100; portCounter++)
				{
					fprintf(stderr, "Bind failed on port %d\n", INITPORT + portCounter - 1);
					servProps = getSockaddrFromPort(INITPORT + portCounter);
				}
			}while(-1 == listen(*listener, 1));
		}
		else{*/
			perror("Listening failed");
		//}
		return -1;
	}
	printf("Now listening on socket\n");
	return INITPORT + portCounter - 1;
}

char *stringToUpper(char *str, int n){ //Makes the first n chars of a string uppercase
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


FILE *fileFromPort(int port, char *outName){
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

