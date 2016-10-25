#include "headers.h"
#define NUMCHLD 10 	//Number of connection handling child processes
#define TIMEOUT 900 //900 seconds = 15 minute timeout


struct prog_stats{
	int size;				//Size of program in bytes
	int compexitstatus;		//Compiler exit status
	char *sourcename;		//Name of the source code file being written
	char *execname;			//Name of the executable file being created
	int progexitstatus;		//Exit status of the users program									//This struct holds metadata about the programs being run
	time_t received;		//Time when program is starting to be received 
	time_t written;			//Time when program was written to disk
	time_t compiled;		//Time when program was compiled
	time_t started;			//Time when program started executing
	time_t finished;		//Time when it finished
	time_t runtime;			//Total runtime of the program
	time_t replytime;		//Time when server finished sending all of the responses to the client
	time_t deleted;			//Time when the data was deleted from disk
};

struct status{
	int statusID;	//Current status of child 													//This struct holds information on the status of the child processes
	time_t start;	//Time when child entered this state
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
FILE *fileFromPort(int port, char *outName);
void printMetadata(int fd, struct prog_stats *metadata);

int main(int argc, char **argv){

	int pid = 0, index = 0; 
	int shmfd;
	struct prog_stats *childstatus = NULL;									//Initialize the shared mem for program metadata to zero
	initsharedmem("metadata", shmfd, struct prog_stats, childstatus, 1);

	int statusshmfd;														//Initialize the shared memory for statuses to zero
	struct status *statuses;
	initsharedmem("status", statusshmfd, struct status, statuses, NUMCHLD);

	if(SIG_ERR == signal(SIGUSR1, status))									//Attempt to register the SIGUSR1 handler
	{
		perror("Registering status printer failed");			
		return EXIT_FAILURE;
	}

	if(SIG_ERR == signal(SIGUSR2, killServerGracefully))					//Attempt to register the SIGUSR2 handler
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
	pid_lock.l_start = 0;										//This is a file lock for the PID table
	pid_lock.l_len = sizeof(int)*(NUMCHLD + 1);					//It maintains mutual exclusion

	meta_lock.l_type = F_WRLCK;
	meta_lock.l_whence = SEEK_SET;
	meta_lock.l_start = 0;										//This is a file lock for the metadata
	meta_lock.l_len = sizeof(struct prog_stats);

	spawnChildren(NUMCHLD);										//Spawn a bunch of children
	pid = fork();
	if(pid > 0){ //Do parent stuff
		while(1){
			if(-1 == (pid = wait(NULL))){						//Wait for a child to exit
				perror("Child exited badly");
			}

			opensharedmem("metadata", shmfd, struct prog_stats, childstatus, 1);	//Open the metadata and lock it
			fcntl(shmfd, F_SETLKW, &meta_lock);

			printf("Child exited with PID: %d\n", (int)pid);						//A PID table is used to give each child an ID. The ID is the index in the table.
			for(index = 0; index < NUMCHLD; index++){
				printf("PID[%d] = %d\n", index, *(PIDTable + index));				//Print the child's PID and the PID table
			}


			fcntl(PIDshmfd, F_SETLKW, &pid_lock);									//Lock the PID table memory, get the new index in the PID table	
			for(index = 0; *(PIDTable + index) != (int)pid && (statuses + index)->statusID != 14 && index < NUMCHLD; index++); //Calculate index
			printf("Deleting index %d\n", index); 
			*(PIDTable + index) = 0;												//Zero out the unused entry in the PID table
			pid_lock.l_type = F_UNLCK;
			fcntl(PIDshmfd, F_SETLK, &pid_lock);


			printMetadata(1, childstatus);
			closesharedmem("metadata", shmfd, struct prog_stats, childstatus, 1); //Print the metadata and close it out
			meta_lock.l_type = F_UNLCK;
			fcntl(shmfd, F_SETLK, &meta_lock);


			spawnChildren(1);	//Spawn a new child to replace the old one
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
	}else if(pid < 0){
		perror("Spawning timeout handler failed, children will not time out");	
	}

}

void killServerGracefully(int signal){	//This sends a SIGTERM signal everywhere when a SIGUSR2 signal is received. SIGTERM is handled by the children to exit gracefully
	printf("SIGUSR2 Received, now killing processes\n");
	kill(0, SIGTERM);
}
void status(int signal){				//This prints out the statuses of the child processes when a SIGUSR1 signal is received
	printf("SIGUSR1 Received, now printing process stats\n");

	int statusshmfd;
	struct status *statuses;
	opensharedmem("status", statusshmfd, struct status, statuses, NUMCHLD);

	int *PIDTable;
	int PIDshmfd;
	opensharedmem("PIDTable", PIDshmfd, int, PIDTable, NUMCHLD);
	printf("Index number\tPID\tStatus\t\t\tTime in State\n");
	for(int i = 0; i < NUMCHLD; i++){
		printf("%d\t\t%d\t%s\t\t\t%lu\n", i, *(PIDTable + i), statusPrinter((statuses+i)->statusID), time(NULL) - (statuses+i)->start);
	}

}
char *statusPrinter(int status){	//This returns the string corresponding to the staus ID
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
int spawnChildren(int num){		//Spawns a number of connection handling children
	int children = 0, pid = 0;



	while(children < num)
	{
		printf("Parent forking a child process...\n");			
		pid = fork();
		if(0 == pid)
		{
			struct flock f_lock;
			f_lock.l_type = F_WRLCK;
			f_lock.l_whence = SEEK_SET;				//Lock used for PID table access
			f_lock.l_start = 0;
			f_lock.l_len = sizeof(int)*(NUMCHLD + 1);

			int statusshmfd;
			struct status *statuses;			//Open the status memory
			opensharedmem("status", statusshmfd, struct status, statuses, NUMCHLD);

			int *PIDTable;
			int PIDshmfd , index;				//Open the PID table
			opensharedmem("PIDTable", PIDshmfd, int, PIDTable, NUMCHLD);

			fcntl(PIDshmfd, F_SETLKW, &f_lock);
			for(index = 0; *(PIDTable + index) != 0 && index < NUMCHLD; index++); //Calculate index
			printf("Now adding PID %d to index %d\n", (int)getpid(), index);
			*(PIDTable + index) = (int)getpid();	//Add the new child PID to the PID table
			f_lock.l_type = F_UNLCK;
			fcntl(PIDshmfd, F_SETLK, &f_lock);
			(statuses + index)->statusID = 1; //Status 1: Initializing
			(statuses + index)->start = time(NULL);

			spawnChild();						//This is the main code for a child

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
	return EXIT_SUCCESS;
}

int spawnChild(){
	const int INITPORT = 10000;
	int listener, connection, port;

	int statusshmfd;
	struct status *statuses;		//Open up the statuses
	opensharedmem("status", statusshmfd, struct status, statuses, NUMCHLD);

	int *PIDTable;
	int PIDshmfd;			//Open up the PID table
	opensharedmem("PIDTable", PIDshmfd, int, PIDTable, NUMCHLD);

	int index;
	for(index = 0; *(PIDTable + index) != getpid() && index < NUMCHLD; index++); //Get the index of the child

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

	handleConnection(listener, connection, port); 		//This handles an incoming connection

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
	struct status *statuses;		//Open up the statuses
	opensharedmem("status", statusshmfd, struct status, statuses, NUMCHLD);

	int *PIDTable;
	int PIDshmfd;					//Open up the PID table
	opensharedmem("PIDTable", PIDshmfd, int, PIDTable, NUMCHLD);

	int index;
	for(index = 0; *(PIDTable + index) != getpid() && index < NUMCHLD; index++); //Get the index of the child

	(statuses + index)->statusID = 4; // Status 4: Connected
	(statuses + index)->start = time(NULL);

	struct prog_stats *metadata = (struct prog_stats *)malloc(sizeof(struct prog_stats));
	memset(metadata, 0, sizeof(struct prog_stats)); //Zero out the struct, in case the user exits immediately
	
	FILE *outFile = NULL;
	int bytesRead, writingCode = 0, returnCode = 0, size = 0, dying = 0;
	char text[1024] = {0}, sourcename[100] = {0}, execname[100] = {0};

	const char start[] = "start", end[] = "exit", stop[] = "stop";
	const char welcome[] = "Welcome to Ian's C auto-compiling server security nightmare!\nType \"%s\" to begin coding and \"%s\" to submit your code and \"%s\" to end the session\n";
	
	dprintf(connection, welcome, start, stop, end); //Print welcome message

	(statuses + index)->statusID = 5; // Status 5: Connected and reading
	(statuses + index)->start = time(NULL);


	void dieGracefully(int signal){	//This is the graceful death handler used by SIGTERM
		if(0 == dying){				//The dying flag makes sure no double close occurs
			if(outFile != NULL){
				fclose(outFile);
			}
			unlink(sourcename);
			unlink(execname);
		}
		dying = 1;

		
		(statuses + index)->statusID = 12; // Status 12: Exiting
		(statuses + index)->start = time(NULL);

		printf("Child process connection now exiting...\n");
		if(-1 == shutdown(connection, SHUT_RDWR))
		{
			perror("Shutdown failed");
		}
		if(-1 == close(connection)){
			perror("Closing connection failed");
		}
		if(-1 == close(listener)){
			perror("Closing listener failed");
		}
		(statuses + index)->statusID = 13; // Status 13: Connection closed
		(statuses + index)->start = time(NULL);
		sleep(1);
		kill(getpid(), SIGKILL);
	}
	if(SIG_ERR == signal(SIGTERM, dieGracefully)){	//Register graceful death handler
		perror("Error registering SIGTERM handler");
	}

	bytesRead = read(connection, text, 1024);		//Start reading input from the user
	while(-1 != bytesRead)
	{

		

		if(0 == strncmp(text, end, strlen(end))){	//Check if user input is the end token
			(statuses + index)->statusID = 11; // Status 11: Ending session
			(statuses + index)->start = time(NULL);

			if(1 == writingCode){
				fclose(outFile);
			}
			break;							//Break out of the loop
		}

		if(1 == writingCode && 0 != strncmp(text, stop, strlen(stop))){	//Check if the user is writing code doesn't want to stop
			fwrite(text, sizeof(char), bytesRead, outFile); 			//Write their code to disk
			size += bytesRead;				
		}

		if(1 == writingCode && 0 == strncmp(text, stop, strlen(stop))){	//Check if the user is writing code and wants to stop
			(statuses + index)->statusID = 7; // Status 7: Connected and stopping coding
			(statuses + index)->start = time(NULL);

			writingCode = 0;
			fclose(outFile);
			metadata->written = time(NULL);
			write(connection, "Code written to file\n", sizeof("Code written to file\n"));
			strncpy(execname, sourcename, strlen(sourcename));								//Close the c file, store some metadata
			*(execname + strlen(execname) - 2) = '\0'; //Chop off .c extension
			metadata->size = size;
			metadata->sourcename = sourcename;
			metadata->execname = execname;	
			(statuses + index)->statusID = 8; // Status 8: Connected and compiling code
			(statuses + index)->start = time(NULL);

			compileCode(sourcename, execname, connection);		//Compile the code

			wait(&returnCode);									//Wait for the compilation to return
			metadata->compiled = time(NULL);
			metadata->compexitstatus = returnCode;
			dprintf(connection, "Your return code is: %d\n", returnCode);	
			metadata->started = time(NULL);
			if(returnCode == 0){
				(statuses + index)->statusID = 9; // Status 9: Connected and executing code
				(statuses + index)->start = time(NULL);
				executeCode(execname, connection);				//Execute the code
			}
			wait(&returnCode);									//Wait for the execution to return
			

			metadata->finished = time(NULL);
			metadata->runtime = metadata->finished - metadata->started;
			metadata->progexitstatus = returnCode;
																//Write some metadata and delete the temp files
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
			meta_lock.l_whence = SEEK_SET;				//This is a lock for writing the metadata to shared memory
			meta_lock.l_start = 0;
			meta_lock.l_len = sizeof(struct prog_stats);

			int shmfd;
			struct prog_stats *shmprogstats;
			opensharedmem("metadata", shmfd, struct prog_stats, shmprogstats, 1);
			fcntl(shmfd, F_SETLKW, &meta_lock);
			
			(statuses + index)->statusID = 10; // Status 10: Connected and writing metadata
			(statuses + index)->start = time(NULL);

			memcpy((void *)shmprogstats, (void *)metadata, sizeof(struct prog_stats));

			shmprogstats -> sourcename = (char *)malloc(strlen(metadata->sourcename) + 1);					//These strncpy and malloc lines make sure that shmprogstats has a 
			strncpy(shmprogstats -> sourcename, metadata->sourcename, strlen(metadata->sourcename) + 1);	//copy of the data being pointed to, rather than just a copy of the pointers
			shmprogstats -> execname = (char *)malloc(strlen(metadata->execname) + 1);
			strncpy(shmprogstats -> execname, metadata->execname, strlen(metadata->execname) + 1); 			

			meta_lock.l_type = F_UNLCK;
			fcntl(shmfd, F_SETLK, &meta_lock);
			
			
			close(shmfd);

			(statuses + index)->statusID = 5; // Status 5: Connected and reading
			(statuses + index)->start = time(NULL);
		}

		if(0 == writingCode && 0 == strncmp(text, start, strlen(start))){	//If the user isn't writing code and wants to start
			outFile = fileFromPort(port, sourcename);						//Create a file and start writing code to it
			printf("Filename received: %s\n", sourcename);		
			if(NULL == outFile){
				perror("Failed to create file");
			}
			else{
				write(connection, "Now writing code to file\n", sizeof("Now writing code to file\n"));

				writingCode = 1;
				metadata->received = time(NULL);
				(statuses + index)->statusID = 6; // Status 6: Connected and writing code
				(statuses + index)->start = time(NULL);

			}

		}
	bytesRead = read(connection, text, 1024);	//Keep reading new input from user
	}
}

void executeCode(char *execname, int connection){	//This executes the given program redirects the output
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

void compileCode(char *sourcename, char *execname, int connection){	//This compiles some code and redirects the output
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
struct sockaddr_in getSockaddrFromPort(int port){	//This returns a sockaddr_in struct for the given port
	struct sockaddr_in out;
	memset(&out, 0, sizeof(out));
	out.sin_port = htons(port);
	out.sin_addr.s_addr = htonl(INADDR_ANY);
	out.sin_family = AF_INET;
	return out;
}

int listenOnPort(int INITPORT, int *listener){	//This attempts to listen on a port and stores the listener in the int listener variable
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
	return INITPORT + portCounter - 1;	//The actual port which is bound to is returned
}

FILE *fileFromPort(int port, char *outName){	//This makes a filename out of a port number and the epoch time
	int offset = 0;								//outName is used to store the created filename. A FILE * to the actual file is returned
 	char filename[100] = {0};
	offset += longToString(filename, port, offset);
	filename[offset] = '-';
	offset++;
	offset += longToString(filename, (long)time(NULL), offset);
	strncpy(filename+offset, ".c\0", 3);	
	offset += 3;
	printf("Trying to create file with name %s\n", filename);
	strncpy(outName, filename, offset);
	return fopen(filename, "w+");
}


void printMetadata(int fd, struct prog_stats *metadata){ //This prints the metadata stored in the prog_stats struct
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

