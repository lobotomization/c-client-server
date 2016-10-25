#ifndef USEFUL_FUNCTIONS_HEADER
#define USEFUL_FUNCTIONS_HEADER
#include <stdio.h>
/*
These macros open, zero out, or close a section of shared memory.
shmname: a char* which is the name of the shared memory
shmfd: an int which is the file descriptor for the shared memory
type: the datatype (e.g. int, char*, struct my_struct) which will be used in this shared memory
arrayname: a variable of type (type *) which will be an array of the datatype
length: how many of the given datatype are stored in the array
*/
#define opensharedmem(shmname, shmfd, type, arrayname, length){\
	if(-1 == ((shmfd) = shm_open((shmname), O_RDWR|O_CREAT, 0666))){\
		perror("shmopen failed");\
	}\
	if(-1 == ftruncate((shmfd), sizeof(type)*(length))){\
		perror("ftruncate failed");\
	}\
	if((type *)-1 == ((arrayname) = (type *)mmap(0, sizeof(type)*(length), PROT_READ | PROT_WRITE, MAP_SHARED, (shmfd), 0))){\
		perror("mmap failed");\
	}\
}\
while(0)

#define initsharedmem(shmname, shmfd, type, arrayname, length){\
	opensharedmem(shmname, shmfd, type, arrayname, length);\
	memset((arrayname), 0, sizeof(type)*(length));\
}\
while(0)

#define closesharedmem(shmname, shmfd, type, arrayname, length){\
	if(-1 == munmap((arrayname), (length)*sizeof(type))){\
		perror("munmap failed");\
	}\
	if(-1 == shm_unlink(shmname)){\
		perror("shm_unlink failed");\
	}\
	if(-1 == close(shmfd)){\
		perror("close failed");\
	}\
}\
while(0)

int toupper(int c){
	if(97 <= c && c <= 122){
		c -= 32;
	}
	return c;
}					//Hand-made tolower and toupper

int tolower(int c){
	if(65 <= c && c <= 90){
		c += 32;
	}
	return c;
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

char *stringToLower(char *str, int n){ //Makes the first n chars of a string lowercase. This function will stop if it encounters a '\0' char
	const char *out = str;
	while(*str != '\0'&& n > 0){
		*str = tolower(*str);
		str++;
		n--;
	}
	return (char *)out;
}

int longToString(char *str, long l, int offset){ //This prints a given long to a section of a string starting at a given offset. This function could go past the end of a string and overwrite the '\0'! Be careful!
	const int longSize = snprintf(NULL, 0, "%ld", l);
	snprintf(str+offset, longSize+1, "%ld", l);
	return (int)longSize;
}
#endif
