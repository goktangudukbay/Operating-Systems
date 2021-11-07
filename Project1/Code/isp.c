/*
 * Intercepting shell program to execute pipe commands in two modes.
 *
 * MUSTAFA GOKTAN GUDUKBAY, 21801740, CS342, SECTION 3
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include<sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>

/*
 * Splits the string using space as a delimiter.
 */

char** splitString(char input[200], int* noOfArguments){
  char** arguments;
  *noOfArguments = 0;
  char commandCopy[200] = {0};
  char* token;
  strcpy(commandCopy, input);

  // Counting the number of arguments (if composite command counting arguments for only the first one)
  
  token = strtok(commandCopy, " ");
  while(token != NULL){
          (*noOfArguments)++;
          token = strtok(NULL, " ");
  }

  int index = 1;
  strcpy(commandCopy, input);
  arguments = malloc(((*noOfArguments) + 1) * sizeof(char *));
  token = strtok(commandCopy, " ");
  arguments[0] = malloc(strlen(token) + 1);
  strcpy(arguments[0], token);
  
  while(index < *noOfArguments){
    token = strtok(NULL, " ");
    arguments[index] = malloc(strlen(token) + 1);
    strcpy(arguments[index++], token);
  }

  arguments[*noOfArguments] = NULL;
  return arguments;
}

int main(int argc, char *argv[])
{
	// Variables
	pid_t pidFirstChild = -1;
	pid_t pidSecondChild = -1;
	char command[200];
	char** arguments1;
	char** arguments2;
	int* noOfArguments = (int*)malloc(sizeof(int));
	int composite;
	int pipe1[2];
	int pipe2[2];
	int character_count;
	int read_call_count;
	int write_call_count;
	
	// Counting variables
	// struct timeval current_time;
	// struct timeval end_time;
	
	// Code
	composite = 0;
	while(1){
    character_count = 0;
    read_call_count = 0;
    write_call_count = 0;
    composite = 0;

    // Getting input from user
	printf("isp$ ");
	fgets(command, sizeof command, stdin);
    if(strcmp(command, "\n") == 0)
      continue;
    // gettimeofday(&current_time, NULL);
		char *newline = strchr( command, '\n' );
		if ( newline )
			*newline = 0;

    if(strcmp(command, "exitISPC") == 0)
      break;

	// Checking if command is composite or not
    if(strstr(command, " | ") != NULL)
      composite = 1;
    if(composite == 1) {
      if (pipe(pipe1) == -1) {
        fprintf(stderr,"Pipe 1 failed");
        return 1;
      }

    if(atoi(argv[2]) == 2){
        if(pipe(pipe2) == -1) {
          fprintf(stderr,"Pipe 2 failed");
          return 1;
        }
      }
    }

	pidFirstChild = fork();

    if((pidFirstChild != 0) && (composite == 1)){
      pidSecondChild = fork();
    }

    if(pidFirstChild == 0){
      // Splitting arguments
      if(composite == 1)
      {
          char commandCopyForFirstArgument[200] = {'\0'};
          int j = 0;
          for(j = 0;; j++){
            if(command[j] == ' ' && command[j+1] == '|')
               break;
            else
              commandCopyForFirstArgument[j] = command[j];
          }
          commandCopyForFirstArgument[j+1] = '\0';
          arguments1 = splitString(commandCopyForFirstArgument, noOfArguments);

          // Normal communication, mode value 1, do nothing

          // Tapped communication, mode value 2
          if(atoi(argv[2]) == 2){
            close(pipe2[0]);
            close(pipe2[1]);
          }

         dup2(pipe1[1], 1);
         close(pipe1[0]);
         execvp(arguments1[0], arguments1);
         printf("\nCommand not found.\n");
         continue;
      }
      else{
        arguments1 = splitString(command, noOfArguments);
			  execvp(arguments1[0], arguments1);
        printf("\nCommand not found.\n");
        continue;
      }
    }
	else if(pidSecondChild == 0){

      // Normal communication, mode value 1
      if(atoi(argv[2]) == 1){
        close(pipe1[1]);
        dup2 (pipe1[0], 0);
      }

      // Tapped communication, mode value 2
      if(atoi(argv[2]) == 2){
        close(pipe1[0]);
        close(pipe1[1]);
        close(pipe2[1]);
        dup2 (pipe2[0], 0);
      }

      char* compositeMark = strstr(command, " | ");
      arguments2 = splitString(&compositeMark[3], noOfArguments);
      execvp(arguments2[0], arguments2);
      printf("\nCommand not found.\n");
      continue;
    }
    else {

      if(composite == 1){
        close(pipe1[1]);

        // Tapped communication, mode value 2
        if(atoi(argv[2]) == 2){
          close(pipe2[0]);
          char* nbyteArray = malloc(atoi(argv[1]));
          memset(nbyteArray,0, atoi(argv[1]));
          int readByte = read(pipe1[0], nbyteArray, atoi(argv[1]));
          while(readByte > 0){
            read_call_count++;
            character_count +=  readByte;
            write_call_count++;
            write(pipe2[1], nbyteArray, atoi(argv[1]));
            memset(nbyteArray,0, atoi(argv[1]));
            readByte = read(pipe1[0], nbyteArray, atoi(argv[1]));
          }

          close(pipe2[1]);

          wait(NULL);
          wait(NULL);

          free(nbyteArray);

          printf("\ncharacter-count: %d\n", character_count);
          printf("read-call-count: %d\n", read_call_count);
          printf("write-call-count: %d\n\n", write_call_count);
        }
      }

      wait(NULL);
      wait(NULL);

      // gettimeofday(&end_time, NULL);
      // double totalTime = 1000000*(end_time.tv_sec - current_time.tv_sec)
      //                   + end_time.tv_usec - current_time.tv_usec;
      // printf("N: %d MODE: %d TIME: %f microseconds\n", atoi(argv[1]), atoi(argv[2]), totalTime);
    }
  }

  free(noOfArguments);
  return 0;
}
