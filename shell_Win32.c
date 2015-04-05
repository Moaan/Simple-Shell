#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <windows.h>
#define bufferSize 4096

char inputBuffer[bufferSize];
int exitShell = 0, argCount = 0, runBackground = 0, streamRedirected = 0;
BOOL handleInheritance = FALSE;
//process info:
STARTUPINFO si;
PROCESS_INFORMATION pi;	
SECURITY_ATTRIBUTES sa;

void CreateChildProcess(char* processName, STARTUPINFO s, PROCESS_INFORMATION p) {
	if (!CreateProcess(NULL, processName,
	NULL, // don’t inherit process handle
	NULL, // don’t inherit thread handle
	handleInheritance, // enable/disable handle inheritance
	0, // no creation flags
	NULL, // use parent’s environment block
	NULL, // use parent’s existing directory
	&s,
	&p))
	{
		fprintf(stderr, "Create Process Failed");
		exit(1);
	}
	
	// parent will wait for the child to complete
	if(runBackground == 0) {
		WaitForSingleObject(p.hProcess, INFINITE);
	} else runBackground = 0;
	
	
	// close handles
	CloseHandle(p.hProcess);
	CloseHandle(p.hThread);
}

//returns the argument count.
int getArgCount() {
	int argc, i;
	argc = i = 0;
	
	//scan ahead preceding spaces if any
	if((inputBuffer[i] != ' ') && (inputBuffer[i] != '\n')) {
		argc++;
		i++;
	}

	while((inputBuffer[i] != '\n')) {
		while(inputBuffer[i] == ' ') 
		{
			i++;
			if((inputBuffer[i] != ' ') && (inputBuffer[i] != '\n')) argc++;
		}
		i++;
	}
	inputBuffer[i] = '\0';
	
	return argc;
}

void restoreAllStreams() {
	//restore input
	HANDLE consoleInput;

	consoleInput = CreateFile("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, &sa, OPEN_EXISTING , FILE_ATTRIBUTE_NORMAL, NULL);
	if(consoleInput == INVALID_HANDLE_VALUE) {
		DWORD errorCode = GetLastError();
		fprintf(stderr, "Error restoring input to console.\nError code: %d", errorCode);
		exit(1);
	}

	SetStdHandle(STD_INPUT_HANDLE, consoleInput);
	
	//restore output
	HANDLE consoleOutput;
	
	consoleOutput = CreateFile("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING , FILE_ATTRIBUTE_NORMAL, NULL);
	if(consoleOutput == INVALID_HANDLE_VALUE) {
		DWORD errorCode = GetLastError();
		fprintf(stderr, "Error restoring input to console.\nError code: %d", errorCode);
		exit(1);
	}

	SetStdHandle(STD_OUTPUT_HANDLE, consoleOutput);
	SetStdHandle(STD_ERROR_HANDLE, consoleOutput);
	
	si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.dwFlags = STARTF_USESTDHANDLES;
	
}

void redirectStream(int i, DWORD replaceHandle, DWORD accessMode, DWORD createDisposition) {
	int c = 0, j = i;
	char fileName[100]; //making it big enough for long file names and paths.
	
	while(inputBuffer[j] == ' ') j++;
	
	while((inputBuffer[j] != ' ')&&(inputBuffer[j] != '\0')) {
		fileName[c++] = inputBuffer[j++]; //retrieve file name
	}
	fileName[c] = '\0';
	
	//check for '&':
	while(inputBuffer[j] != '\0') {
		if(inputBuffer[j++] == '&') runBackground = 1;
	}
	
	//setting up CreateFile() parameters for the file:
	DWORD share = FILE_SHARE_READ; //other processes allowed to read during execution
	DWORD flags = FILE_ATTRIBUTE_NORMAL;
	HANDLE template = NULL; //ignored for existing file.
	
	HANDLE fileHandle = CreateFile(fileName, accessMode, share, &sa, createDisposition, flags, template);
	if(fileHandle == INVALID_HANDLE_VALUE) {
		DWORD errorCode = GetLastError();
		fprintf(stderr, "Error opening %s\nError code: %d", fileName, errorCode);
		exit(1);
	}
	
	SetStdHandle(replaceHandle, fileHandle);
	
	si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.dwFlags = STARTF_USESTDHANDLES;
	
	inputBuffer[i-1] = inputBuffer[i] = '\0'; //don't want process to consider remaining "args" except &
	
	streamRedirected = 1;
	handleInheritance = TRUE;
	
	CreateChildProcess(inputBuffer, si, pi);
	
	CloseHandle(fileHandle);
}

void pipeProcesses(int i) {
	//pipe parameters
	HANDLE ReadHandle, WriteHandle;
	//second process info:
	STARTUPINFO si2;
	PROCESS_INFORMATION pi2;
	char processOne[100], processTwo[100];
	int c = 0, h = 0, j = i;

	ZeroMemory(&si2, sizeof(si2));
	si2.cb = sizeof(si2);
	ZeroMemory(&pi2, sizeof(pi2));
	
	//retrieve first process name
	while(h < i) processOne[c++] = inputBuffer[h++];
	inputBuffer[h] = ' ';
	processOne[c] = '\0';
	
	c = 0;
	//retrieve second process name
	while(inputBuffer[j] == ' ') j++;
	while((inputBuffer[j] != ' ')&&(inputBuffer[j] != '\0')) {
		processTwo[c++] = inputBuffer[j++];
	}
	processTwo[c] = '\0';
	
	/*//check for '&':
	while(inputBuffer[j] != '\0') {
		if(inputBuffer[j++] == '&') runBackground = 1;
	}*/
	
	//create pipe
	if(!CreatePipe(&ReadHandle, &WriteHandle, &sa, 0)){
		fprintf(stderr, "Create Pipe Failed\n");
		exit(1);
	}
	
	GetStartupInfo(&si);
	GetStartupInfo(&si2);
	
	streamRedirected = 1;
	handleInheritance = TRUE;
	
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	si.hStdOutput = WriteHandle;
	si.dwFlags = STARTF_USESTDHANDLES;
	
	CreateChildProcess(processOne, si, pi);
	
	si2.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si2.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	si2.hStdInput = ReadHandle;
	si2.dwFlags = STARTF_USESTDHANDLES;
	
	CreateChildProcess(processTwo, si2, pi2);
}
//get the arguments from the inputBuffer buffer and save to array of char arrays.
void parseInput() {
	int i = 0;
 
	for(i = 0;(inputBuffer[i] != '\0'); i++)
	{
		while(inputBuffer[i] == ' ') i++;//skip ahead blanks
		if(inputBuffer[i] == 'e') { //check for "exit"
			if((inputBuffer[i+1]=='x')&&(inputBuffer[i+2]=='i')&&(inputBuffer[i+3]=='t')) exitShell = 1;
		} else if(inputBuffer[i] == '&') {
			runBackground = 1;
			inputBuffer[i] = '\0';
		} else if((inputBuffer[i] == '>') && (inputBuffer[i+1] == ' ')) {
			redirectStream(i+1, STD_OUTPUT_HANDLE, GENERIC_WRITE, CREATE_ALWAYS);
		} else if((inputBuffer[i] == '>') && (inputBuffer[i+1] == '>')) {
			inputBuffer[i+1] = ' ';
			redirectStream(i+1, STD_OUTPUT_HANDLE, FILE_APPEND_DATA, OPEN_EXISTING);
		} else if((inputBuffer[i] == '2') && (inputBuffer[i+1] == '>')) {
			inputBuffer[i+1] = ' ';
			redirectStream(i+1, STD_ERROR_HANDLE, GENERIC_WRITE, CREATE_ALWAYS);
		} else if((inputBuffer[i] == '2') && (inputBuffer[i+1] == '>') && (inputBuffer[i+2] == '>')) {
			inputBuffer[i+1] = ' ';
			redirectStream(i+1, STD_ERROR_HANDLE, FILE_APPEND_DATA, OPEN_EXISTING);
		} else if((inputBuffer[i] == '<') && (inputBuffer[i+1] == ' ')) {
			redirectStream(i+1, STD_INPUT_HANDLE, GENERIC_READ, OPEN_EXISTING);
		} else if((inputBuffer[i] == '|') && (inputBuffer[i+1] == ' ')) {
			pipeProcesses(i);
		}
	}
}

int main() {
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));
	
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;
	
	//Main part of the program:
	do {
		printf("\nMyshell>");
		fgets(inputBuffer, bufferSize, stdin);

		argCount = getArgCount();
		
		if(argCount != 0) {
			parseInput();
			
			if(exitShell) {
				printf("Goodbye\n");
				break;
			}
			
			if(streamRedirected == 0) {
				CreateChildProcess(inputBuffer, si, pi); 
			}else { //reset redirected handles if any
				streamRedirected = 0;
				restoreAllStreams();
			}
		}
		
	} while(1);

	
	return 0;
}