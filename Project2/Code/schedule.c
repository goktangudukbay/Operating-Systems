/*
* Multi-threaded scheduling simulator
* There will be N threads running concurrently and generating cpu
* bursts (workload). There will be one server thread that will be
* responsible from scheduling and executing the bursts.
* Mustafa Goktan Gudukbay, 21801740, Section 3
*/
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>

struct cpuBurst {
  int threadIndex;
  int burstIndex;
  float length;
  double generationTime;
  struct cpuBurst* next;
};

struct wThreadParameter {
  int threadIndex;
  float** burstInformations;
  int noOfBursts;
};

//global variables
int wNumber;
struct cpuBurst* readyQueue;
struct cpuBurst* endOfReadyQueue;
int burstsToBeGenerated;
int minB, avgB, minA, avgA;
char algo[15];

//to understand if reading from a file, 1 is for reading from a file and
//0 is for generating random bursts
int readFromFile;

//vRuntime Variable
int* virtualRunTimes;

//lock variable
pthread_mutex_t lock;

//condition variables
pthread_cond_t condition;

void* createBurst(void* argument){
  struct timeval current_time;
  float sleepTime;

  if(readFromFile == 1){
    struct wThreadParameter* para = (struct wThreadParameter*)argument;

    for(int i = 0; i < para->noOfBursts; i++){
      //add the new burst to the end of the queue
      struct cpuBurst* newBurst = malloc(sizeof(struct cpuBurst));

      newBurst->next = NULL;
      newBurst->threadIndex = para->threadIndex;
      newBurst->burstIndex = i+1;

      usleep(para->burstInformations[i][0]*1000);

      //lock
      pthread_mutex_lock(&lock);

      newBurst->length = para->burstInformations[i][1];

      if(readyQueue == NULL){
        readyQueue = newBurst;
        endOfReadyQueue = readyQueue;
      }
      else{
          endOfReadyQueue->next = newBurst;
          endOfReadyQueue = newBurst;
      }

      gettimeofday(&current_time, NULL);
      newBurst->generationTime = (current_time.tv_usec + (current_time.tv_sec * 1000000))/1000.0;

      //signal
      pthread_cond_signal(&condition);
      //unlock
      pthread_mutex_unlock(&lock);
    }
    for(int i = 0; i < para->noOfBursts; i++)
      free(para->burstInformations[i]);
    free(para->burstInformations);
    free(para);
  }
  else{
      float burstLength;

      srand((unsigned) time(NULL) ^ (*(int*)argument));

      for(int i = 0; i < burstsToBeGenerated; i++){
        sleepTime = log(1-((float)rand()/RAND_MAX))/(-1.0/avgA);
        while(sleepTime < minA)
          sleepTime = log(1-((float)rand()/RAND_MAX))/(-1.0/avgA);

        usleep(sleepTime*1000);

        //add the new burst to the end of the queue
        struct cpuBurst* newBurst = malloc(sizeof(struct cpuBurst));

        newBurst->next = NULL;
        newBurst->threadIndex = *(int*)argument;
        newBurst->burstIndex = i+1;

        burstLength = log(1-((float)rand()/RAND_MAX))/(-1.0/avgB);
        while(burstLength < minB)
          burstLength = log(1-((float)rand()/RAND_MAX))/(-1.0/avgB);

        newBurst->length = burstLength;

        //lock
        pthread_mutex_lock(&lock);

        if(readyQueue == NULL){
          readyQueue = newBurst;
          endOfReadyQueue = readyQueue;
        }
        else{
            endOfReadyQueue->next = newBurst;
            endOfReadyQueue = newBurst;
        }

        gettimeofday(&current_time, NULL);
        newBurst->generationTime = (current_time.tv_usec + (current_time.tv_sec * 1000000))/1000.0;

        //signal
        pthread_cond_signal(&condition);
        //unlock
        pthread_mutex_unlock(&lock);

      }
      free((int*)argument);
  }

  pthread_exit(NULL);
}

void* schedule(void* argument){

  for(int i = 0; i < *(int*)argument; i++){

    //lock
    pthread_mutex_lock(&lock);

    while(readyQueue == NULL){
      pthread_cond_wait(&condition, &lock);
    }

    float lengthToSleep;
    double waitingTime;
    struct timeval current_time;
    int executedThreadIndex, executedBurstIndex;
    double executedGenerationTime;

    //FCFS
    if(strcmp("FCFS", algo) == 0){
      int threadIndex;
      int burstIndex;
      lengthToSleep = readyQueue->length;
      executedThreadIndex = readyQueue->threadIndex;
      executedBurstIndex = readyQueue->burstIndex;
      executedGenerationTime = readyQueue->generationTime;

      if(readyQueue == endOfReadyQueue){
        free(readyQueue);
        endOfReadyQueue = NULL;
        readyQueue = NULL;
      }
      else{
        struct cpuBurst* temp = readyQueue->next;
        free(readyQueue);
        readyQueue = temp;
      }
    }
    else if((strcmp("SJF", algo) == 0) || (strcmp("PRIO", algo) == 0) || (strcmp("VRUNTIME", algo) == 0)){
      struct cpuBurst* toBeExecuted = NULL;
      struct cpuBurst* temp;

      //if sjf time determines the burst to execute
      if(strcmp("SJF", algo) == 0){
        struct cpuBurst* earliestForAThread;
        for(int i = 1; i <= wNumber; i++){
          temp = readyQueue;
          earliestForAThread = NULL;
          while(temp!= NULL){
            if(temp->threadIndex == i){
              if(earliestForAThread == NULL)
                earliestForAThread = temp;
              else if(earliestForAThread->generationTime > temp->generationTime)
                earliestForAThread = temp;
            }
            temp = temp->next;
          }
          if(earliestForAThread != NULL ){
            if(toBeExecuted == NULL)
              toBeExecuted = earliestForAThread;
            else if(toBeExecuted->length > earliestForAThread->length)
              toBeExecuted = earliestForAThread;
          }
        }
      }
      //if prio thread index determines the burst to execute
      else if(strcmp("PRIO", algo) == 0){
        struct cpuBurst* earliestForAThread;
        for(int i = 1; i <= wNumber; i++){
          temp = readyQueue;
          earliestForAThread = NULL;
          while(temp!= NULL){
            if(temp->threadIndex == i){
              if(earliestForAThread == NULL)
                earliestForAThread = temp;
              else if(earliestForAThread->generationTime > temp->generationTime)
                earliestForAThread = temp;
            }
            temp = temp->next;
          }
          if(earliestForAThread != NULL ){
              toBeExecuted = earliestForAThread;
              break;
          }
        }
      }
      //if virtual run time  determines the burst to execute
      else{
        struct cpuBurst* earliestForAThread;
        for(int i = 1; i <= wNumber; i++){
          temp = readyQueue;
          earliestForAThread = NULL;
          while(temp!= NULL){
            if(temp->threadIndex == i){
              if(earliestForAThread == NULL)
                earliestForAThread = temp;
              else if(earliestForAThread->generationTime > temp->generationTime)
                earliestForAThread = temp;
            }
            temp = temp->next;
          }
          if(earliestForAThread != NULL ){
            if(toBeExecuted == NULL)
              toBeExecuted = earliestForAThread;
            else if(virtualRunTimes[(toBeExecuted->threadIndex)-1] > virtualRunTimes[(earliestForAThread->threadIndex)-1])
              toBeExecuted = earliestForAThread;
          }
        }
        virtualRunTimes[(toBeExecuted->threadIndex) - 1] += toBeExecuted->length*(0.7+(0.3*toBeExecuted->threadIndex));
      }

      lengthToSleep = toBeExecuted->length;
      executedThreadIndex = toBeExecuted->threadIndex;
      executedBurstIndex = toBeExecuted->burstIndex;
      executedGenerationTime = toBeExecuted->generationTime;

      if(readyQueue == endOfReadyQueue){
        free(readyQueue);
        endOfReadyQueue = NULL;
        readyQueue = NULL;
        toBeExecuted = NULL;
        temp = NULL;
      }
      else{
        struct cpuBurst* cur = readyQueue->next;
        temp = readyQueue;
        if(temp == toBeExecuted){
          readyQueue = readyQueue->next;
        }
        else{
          while(temp->next != toBeExecuted){
            temp = temp->next;
          }
          if(toBeExecuted == endOfReadyQueue)
            endOfReadyQueue = temp;
          temp->next = toBeExecuted->next;
        }
        free(toBeExecuted);
      }
    }

    //unlock
    pthread_mutex_unlock(&lock);

    //execute
    gettimeofday(&current_time, NULL);
    waitingTime = ((current_time.tv_usec + (current_time.tv_sec * 1000000))/1000.0)-(executedGenerationTime);
    printf("Burst Number: %d, Thread Index: %d, Burst Index: %d, Burst Length: %f ms, Arrival Time: %lf ms, Waiting Time: %lf ms\n", i+1,
    executedThreadIndex, executedBurstIndex, lengthToSleep, executedGenerationTime, waitingTime);
    //printf("%lf\n", waitingTime);

    usleep(lengthToSleep*1000);
  }

  free((int*)argument);
  pthread_exit(NULL);
}

float** readInformationFromFile(int index, char fileName[], int* numberOfLines){
  //building the file name
  char threadIndexStr[3];
  sprintf(threadIndexStr, "%d", index);
  char burstInfoFile[255];
  strcpy(burstInfoFile, fileName);
  strcat(burstInfoFile, threadIndexStr);
  strcat(burstInfoFile, ".txt");

  FILE *fp = fopen(burstInfoFile,"r");

  *numberOfLines = 0;
  char c = getc(fp);
  char lc = '\n';
  while(c != EOF){
      if ((c == '\n') && (lc != '\n'))
          *numberOfLines = (*numberOfLines) + 1;
      lc = c;
      c = getc(fp);
  }
  fclose(fp);

  //opening the file again to get information
  fp = fopen(burstInfoFile,"r");
  char word[20];

  //creating the array from the file for burst information, each row
  //corresponds to a burst
  float** burstInformations;
  burstInformations = malloc(sizeof(float*) * (*numberOfLines));
  for(int i = 0; i < *numberOfLines; i++){
    burstInformations[i] = malloc(sizeof(float) * 2);
    for(int j = 0; j < 2; j++){
      float temp;
      fscanf(fp, "%s", word);
      while(word[0] == '\n')
        fscanf(fp, "%s", word);
      temp = atof(word);
      burstInformations[i][j] = temp;
    }
  }

  fclose(fp);
  return burstInformations;
}



int main(int argc, char *argv[]){

    //variables
    pthread_t* wthreadArray;
    pthread_t sThread;
    pthread_attr_t attr;
    int* numberOfCPUBurstToBeServed;

    //code
    readyQueue = NULL;
    endOfReadyQueue = NULL;
    wNumber = atoi(argv[1]);

    pthread_cond_init( &condition, NULL);


    if (pthread_mutex_init(&lock, NULL) != 0) {
            printf("\n mutex init has failed\n");
            return 1;
    }

    wthreadArray = malloc(sizeof(pthread_t) * wNumber);
    pthread_attr_init (&attr);

    numberOfCPUBurstToBeServed = malloc(sizeof(int));

    //checking for reading from file
    if(strcmp("-f", argv[3]) == 0){
      //file name for reading from a failed
      char fileName[255];
      strcpy(algo, argv[2]);
      strcpy(fileName, strcat(argv[4], "-"));
      readFromFile = 1;
      *numberOfCPUBurstToBeServed = 0;
      for(int i = 0; i < wNumber; i++){
        struct wThreadParameter* para = malloc(sizeof(struct wThreadParameter));
        int* noOfLines = malloc(sizeof(int));
        para->burstInformations = readInformationFromFile(i+1, fileName, noOfLines);
        para->noOfBursts = *noOfLines;
        para->threadIndex = i+1;
        *numberOfCPUBurstToBeServed += *noOfLines;
        pthread_create (&wthreadArray[i], &attr, createBurst, para);
        free(noOfLines);
      }
    }
    else{
      burstsToBeGenerated = atoi(argv[2]);
      minB = atoi(argv[3]);
      avgB = atoi(argv[4]);
      minA = atoi(argv[5]);
      avgA = atoi(argv[6]);
      strcpy(algo, argv[7]);
      readFromFile = 0;
      *numberOfCPUBurstToBeServed = wNumber * burstsToBeGenerated;
      for(int i = 0; i < wNumber; i++){
        int* threadIndex = malloc(sizeof(int));
        *threadIndex = i+1;

        pthread_create (&wthreadArray[i], &attr, createBurst, threadIndex);
      }
    }

    if(strcmp(algo, "VRUNTIME") == 0){
      virtualRunTimes = calloc(wNumber, sizeof(int));
    }


    pthread_create (&sThread, &attr, schedule, numberOfCPUBurstToBeServed);

    for(int i = 0; i < wNumber; i++)
      pthread_join (wthreadArray[i], NULL);

    pthread_join (sThread, NULL);

    free(wthreadArray);
    if(strcmp(algo, "VRUNTIME") == 0)
      free(virtualRunTimes);

    pthread_attr_destroy(&attr);

    return 0;
}
