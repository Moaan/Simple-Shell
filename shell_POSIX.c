#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#define bufferSize 4096

char inputBuffer[bufferSize], **myargv; //count spaces to determine argc
int argCount = 0;
int runBackground = 0;
int processNotStarted = 1;

//returns the argument count or negative if error occurs.
int getArgCount(char *input) {
	int argc, i;
	argc = i = 0;
	
	//scan ahead preceding spaces if any
	if((input[i] != ' ') && (input[i] != '\n')) {
		argc++;
		i++;
	}

	while((input[i] != '\n')) {
		while(input[i] == ' ') 
		{
			i++;
			if((input[i] != ' ') && (input[i] != '\n')) argc++;
		}
		i++;
	}
	input[i] = '\0';

	return argc;
}

//gets the arguments from the input buffer and saves to array of char arrays
void parseInput(char *input, char **myargv) {
	//use cstring
	int i, l, j, k;
	i = l = j = k = 0;
 
	for(i = 0;(input[i] != '\0'); i++)
	{
		while((input[i] != ' ') && (input[i] != '\0'))
		{
			l++; //count number of chars to allocate
			i++; //skip ahead if reading blank
		}
		if(l)
		{
			*(myargv + j) = malloc((l+1) * sizeof(*myargv)); //allocate according to the measured string length
			for(k = 0;l; k++) {
				myargv[j][k] = input[i-l];
				l--;
			}
			myargv[j][k] = '\0';
			j++;
		}	
	}

	if(strcmp(myargv[argCount-1], "&") == 0) {
		runBackground = 1;
		myargv[argCount-1] = NULL;
		argCount--;
	}
}

void redirectStream(int ind, int streamType, int openFlag) {
	//ind: where in user input the redirection operator is. Will assume index+1 is where file is.
	//streamType: 0 is standard input, 1 is standard output, 2 is standard error
	//openFlag: expecting 0_RDONLY, O_WRONLY or O_APPEND
	
	int targetFile;
	
	//open target file and create it if doesn't exist
	if(openFlag == O_APPEND) targetFile = open(myargv[ind+1], O_APPEND | O_WRONLY); 
	else targetFile = open(myargv[ind+1], openFlag); 
	if(targetFile == -1) {  	//file doesn't exist so create it
   	targetFile = open(myargv[ind+1], O_CREAT | O_RDWR, S_IRWXU); 
   }
   
   dup2(targetFile, streamType);
   
   myargv[ind] = NULL;
   
	execvp(myargv[0], myargv);
	
   close(streamType);
	processNotStarted = 0;
}

void pipeProcesses(int ind) {//gets the index where '|' was found
	int	pid1, pid2, i, j;
	int	pipeID[2];
	char **pr1Args, **pr2Args;
	
	if ((pipe(pipeID)) == -1) { 
		perror("error with pipe\n");
		exit(1);
	}

	pid1 = fork();	
	if(pid1 < 0) {
		printf("Error forking\n");
		exit(1);
	}

	if (pid1 == 0) { /* The first child process; the one getting input (the process afte the |) */
		close (0);
		dup (pipeID[0]);
		close (pipeID[0]);
		close(pipeID[1]);
		pr2Args = malloc((argCount-ind-1)*sizeof(*pr2Args));
		for(i=ind+1, j=0;i<argCount;i++, j++) pr2Args[j] = myargv[i];
		execvp(pr2Args[0], pr2Args);
	}

	//parent process: make second child process
	pid2 = fork();	
	if(pid2 < 0) {
		printf("Error forking\n");
		exit(1);
	}

	myargv[ind] = NULL; //"|" is not an arg for any process

	if(pid2 == 0) { /*The second chil process; the one sending output (the process before the |)*/
		close (1);
		dup(pipeID[1]);
		close(pipeID[0]);
		close(pipeID[1]);
		pr1Args = malloc((ind)*sizeof(*pr1Args));
		for(i=0, j=0;i<ind;i++, j++) pr1Args[j] = myargv[i];
		execvp(pr1Args[0], pr1Args);
	}
	/* back to the parent again */
	if(runBackground == 0) {
		close(pipeID[0]);
		close(pipeID[1]);
		wait(pid1,0,0);
		wait(pid2,0,0);
	} else runBackground = 0;
	
	processNotStarted = 0;
}

int main() {
	pid_t pid;
	int i;

	//The main part of the program:
	do {
		printf("\nMyshell>");
		fgets(inputBuffer, bufferSize, stdin);

		argCount = getArgCount(inputBuffer);
		
		if(argCount != 0) {
			//This needs to be allocated on the top-level!! (don't know what this comment means anymore)
			myargv = malloc(argCount*sizeof(myargv)); 

			parseInput(inputBuffer, myargv);
			
			if(strcmp(myargv[0], "exit") == 0) {
				printf("Goodbye\n");
				break;
			}
			
			//check for pipe |
			for(i = 1; i < argCount-1; i++) {
				if(strcmp(myargv[i], "|") == 0) {
					pipeProcesses(i);
					i = argCount-1;
				}
			}
		
			if(processNotStarted) {
		
				pid = fork();
			
				if(pid < 0) {//error occurred
					printf("Error forking\n");
					return 1;
				} else if(pid == 0) { //child process
				
					//This section checks for special shell commands: &, >, >>, <
					for(i = 1; i < argCount-1; i++) {  //assuming first and last args are processes or files/arguments
						if(strcmp(myargv[i], "<") == 0) redirectStream(i, 0, O_RDONLY);
						else if(strcmp(myargv[i], ">") == 0) redirectStream(i, 1, O_WRONLY);
						else if(strcmp(myargv[i], ">>") == 0) redirectStream(i, 1, O_APPEND);
						else if(strcmp(myargv[i], "2>") == 0) redirectStream(i, 2, O_WRONLY);
						else if(strcmp(myargv[i], "2>>") == 0) redirectStream(i, 2, O_APPEND);			
					}
			
					if(processNotStarted) execvp(myargv[0], myargv);
					else processNotStarted = 1;
				}
				else { //parent process
					if(runBackground == 0) {
						wait(NULL);
					}else runBackground = 0;
				}
			}else processNotStarted = 1;	
		}

	} while(1);
	
	return 0;
}