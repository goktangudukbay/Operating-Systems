#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/wait.h>

#include "sbmem.h"


int main()
{

    int i, j, k, ret; 

    printf("------------------- SIMPLE TEST STARTED -------------------\n");
    char *p1, *p2;  

    ret = sbmem_open(); 
    if (ret == -1)
        exit (1); 
    printf("Simple test: sbmem_open() done\n");

    p1 = sbmem_alloc (256);
    printf("Simple test: sbmem_alloc(256) done\n");

    p2 = sbmem_alloc (128);
    printf("Simple test: sbmem_alloc(128) done\n");

    
    for (i = 0; i < 256; ++i)
        p1[i] = 'A';

    for (i = 0; i < 128; ++i)
        p2[i] = 'B';
    
    for (i = 0; i < 256; ++i)
        if (p1[i] != 'A')
        {
            printf("Simple test failed: case A\n");
            exit(1);
        }
            
    for (i = 0; i < 128; ++i)
        if (p2[i] != 'B')
        {
            printf("Simple test failed: case B\n");
            exit(1);
        }

    sbmem_free(p1);
    printf("Simple test: sbmem_free(p1) done\n");
    
    sbmem_free(p2);
    printf("Simple test: sbmem_free(p2) done\n");

    sbmem_close(); 
    printf("Simple test: sbmem_close() done\n");
    printf("------------------- SIMPLE TEST FINISHED -------------------\n\n");

    
    printf("------------------- COMPLEX TEST STARTED -------------------\n");
    char *p[10];
    int sizes[10];
    int size;
    for(i=0; i < 5; i++) 
    {
        if(fork() == 0)
        {   
            srand(time(NULL));

            ret = sbmem_open();
            if (ret == -1)
                exit (1); 
            printf("Process %d: sbmem_open() done\n", i);


            for(j = 0; j < 10; j++)
            {
                while((size = rand() % 4097) < 128);
                sizes[j] = size;
                p[j] = sbmem_alloc(size);

                for (k = 0; k < size; k++)
                    p[j][k] = i + '0';

                usleep(100);
            }
            printf("Process %d: sbmem_alloc() done\n", i);


            for(j = 0; j < 10; j++)
            {
                for (k = 0; k < sizes[j]; k++)
                    if ( p[j][k] != i + '0')
                    {
                        printf("Complex test failed\n");
                        exit(1);    
                    }
            }

            for(j = 0; j < 10; j++)
            {
                sbmem_free(p[j]);
                usleep(100);
            }
            printf("Process %d: sbmem_free() done\n", i);

            sbmem_close();
            printf("Process %d: sbmem_close() done\n", i);
            exit(0);
        }
    }

    for(int i=0;i<5;i++)
        wait(NULL);

    printf("------------------- COMPLEX TEST FINISHED -------------------\n\n");

    return (0); 
}