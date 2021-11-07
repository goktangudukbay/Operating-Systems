#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <math.h>
#include <string.h>
#include <semaphore.h>
#include <errno.h>
#include <stdint.h>

/*
    CS342 Spring 2021 Project 3
    Mustafa Göktan Güdükbay 21801740 Section 3
    Musa Ege Ünalan         21803617 Section 3

*/
typedef struct AvailNode
{
    struct AvailNode *next;
    struct AvailNode *prev;
} AvailNode;

typedef struct Available
{
    AvailNode *head;
    AvailNode *tail;
} Available;

// Define a name for your shared memory; you can give any name that start with a slash character; it will be like a filename.
const char *S_MEM_NAME = "/sharedMem";
const char *SEM_NAME = "/namedSem";

// Define semaphore(s)
sem_t *sem;

// Define your stuctures and variables.
void *BASE_POINTER;
Available *avail;
int segmentSize;

// Used for experimentation
/*
const int EXPERIMENT = 0;
const char *EXP_MEM_NAME = "/expMem";
int *experiment_count;
int *experiment_size;
*/

int findContainingPowerOf2(int n)
{
    return (int)log2(n - 1) + 1;
}

int sbmem_init(int segmentsize)
{
    // segmentsize must be a power of 2
    // and between 32768 (32KB) and 262144 (256KB)
    if (!(32768 <= segmentsize && segmentsize <= 262144) || segmentsize % 2)
    {
        return -1;
    }

    int fd = shm_open(S_MEM_NAME, O_RDWR | O_CREAT | O_TRUNC, 0666);

    if (fd == -1)
    {
        return -1;
    }

    if (ftruncate(fd, segmentsize) == -1)
    {
        return -1;
    }

    void *segment = mmap(NULL, segmentsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (segment == MAP_FAILED)
    {
        return -1;
    }

    // close the file descriptor for the shared memory after mapping it
    if (close(fd) == -1) {
        return -1;
    }

    // in use
    *((u_int32_t *)segment) = 1;
    // k value, initialized to the correct value after calculating overhead size
    *(((u_int32_t *)segment) + 1) = 0;
    // segment size
    *(((u_int32_t *)segment) + 2) = segmentsize;
    // process count
    *(((u_int32_t *)segment) + 3) = 0;

    // calculate how many different memory block sizes we can have
    // for example for 32KB, 32KB / 128 = 2^8, there can be 8 different sizes
    // we can have 128, 256, 512, 1024, 2048, 4096, 8192, 16384 Byte memory blocks
    // we won't need a block with the full size of the segment, as we use the first
    // memory block for book-keeping purposes
    int numMemBlockSizes = findContainingPowerOf2(segmentsize / 128);

    // linked lists of free memory blocks
    avail = (Available *)(((u_int32_t *)segment) + 4);
    memset(avail, 0, sizeof(Available) * numMemBlockSizes);

    int overheadSize = sizeof(u_int32_t) * 4 + sizeof(Available) * numMemBlockSizes;
    // byte allign the overhead size
    overheadSize += 8 - (overheadSize % 8);

    // calculate and set the k value for the first memory block
    int k = findContainingPowerOf2(overheadSize);
    *(((u_int32_t *)segment) + 1) = k;

    int tmp = segmentsize;
    // split the lower half of memory into another memory block
    // continue with the upper half
    // at the end we have an upper block in use that is containing the segment overhead
    // with the minimum largest enough power of two size
    while (tmp > (1 << k) && tmp > 128)
    {
        void *buddy = (void *)(((uintptr_t)segment) + (tmp / 2));

        // set buddy not in use
        *((u_int32_t *)buddy) = 0;
        // set buddy k value
        *(((u_int32_t *)buddy) + 1) = findContainingPowerOf2(tmp / 2);

        // subtract the segment base address from buddy addresses when storing them in the linked list
        // so we actually store the relative distances of the nodes from the base of the segment
        // and different processes can correctly address them
        avail[findContainingPowerOf2(tmp / 2) - 7].head = (AvailNode *)((uintptr_t)buddy - (uintptr_t)segment);
        avail[findContainingPowerOf2(tmp / 2) - 7].tail = (AvailNode *)((uintptr_t)buddy - (uintptr_t)segment);
        // initalize the AvailNode of the buddy
        *((AvailNode *)(((u_int32_t *)buddy) + 2)) = (AvailNode){0, 0};
        tmp = tmp / 2;
    }

    // unmap the memory segment from memory
    if (munmap(segment, segmentsize) == -1) {
        return -1;
    }


    // initialize the semaphore with the value 1
    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        return -1;
    }

    return 0;
}

int sbmem_remove()
{
    // unlink sempahore and shared memory
    if (shm_unlink(S_MEM_NAME) == -1) {
        return -1;
    }

    if (sem_unlink(SEM_NAME) == -1) {
        return -1;
    }

    return 0;
}

int sbmem_open()
{
    int fd = shm_open(S_MEM_NAME, O_RDWR, 0666);
    if (fd == -1)
    {
        return -1;
    }

    // map the first 16 Bytes of the shared memory
    // used tag is the 1st 4 Bytes
    // k value is the 2nd 4 Bytes
    // segment size is the 3rd 4 Bytes
    // process count is the 4th 4 Bytes
    void *segment = mmap(NULL, 16, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (segment == MAP_FAILED)
    {
        return -1;
    }

    segmentSize = *(((u_int32_t *)segment) + 2);

    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED)
    {
        return -1;
    }

    if (sem_wait(sem) == -1)
    {
        return -1;
    }

    u_int32_t *pCount = (((u_int32_t *)segment) + 3);
    // if there are already 10 processes using the library
    // don't allow sbmem_open, return -1
    if (*pCount == 10)
    {
        sem_post(sem);
        return -1;
    }
    *pCount += 1;

    if (sem_post(sem) == -1)
    {
        return -1;
    }

    // unmap the first 16 Bytes, and map again with the correct segment size
    if (munmap(segment, 16) == -1) {
        return -1;
    }
    
    BASE_POINTER = mmap(NULL, segmentSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (BASE_POINTER == MAP_FAILED)
    {
        return -1;
    }

    avail = (Available *)(((u_int32_t *)BASE_POINTER) + 4);

    if (close(fd) == -1)
    {
        return -1;
    }

    return 0;
}

// our implementation of the Buddy System Reservation Algorithm,
// from Donald Knuth's The Art Of Computer Programming I
void *sbmem_alloc(int size)
{
    // we need to store 2 extra integers at the beginning of each memory block
    // one for used value, if used 1, if free 0
    // one for k value, 2^k being the size of the memory block
    int sizeToBeAllocated = size + 2 * sizeof(u_int32_t);

    // valid memory requests sizes are between 128 and 4096 bytes
    if (size < 128 || size > 4096) {
        return NULL;
    }

    // R1 Part of the algorithm, find a memory block large enough
    // for the given size

    // j is the array index, will check size being smaller than 2^(j+7)
    int numMemBlockSizes = findContainingPowerOf2(segmentSize / 128);
    int j = 0;
    sem_wait(sem);
    for (; j < numMemBlockSizes; j++)
    {
        // kinda problematic, as offset + base_pointer is never going to be zero
        // but as the head is never going to be equal to base_pointer, and the offset won't be zero
        // so we can assume a 0x0 offset at head is a NULL pointer
        //if (((1 << (j + 7)) >= sizeToBeAllocated) && (char *)avail[j].head + (long int)BASE_POINTER != NULL)
        if (((1 << (j + 7)) >= sizeToBeAllocated) && avail[j].head != NULL)
        {
            break;
        }
    }

    // could not find a block return null
    if (j == numMemBlockSizes)
    {
        sem_post(sem);
        return NULL;
    }

    //R2 Part remove from list

    // allocate the first block on the list
    AvailNode *allocated = (AvailNode *)((uintptr_t)avail[j].head + (uintptr_t)BASE_POINTER);
    avail[j].head = ((AvailNode *)(((uintptr_t)allocated) + 8))->prev;

    // if the list isn't empty, set the new heads next to NULL
    if (avail[j].head != NULL)
    {
        ((AvailNode *)(((uintptr_t)avail[j].head) + (uintptr_t)BASE_POINTER + 8))->next = NULL;
    }

    // if the removed node was equal to the tail, then the list is empty
    if (allocated == (AvailNode *)((uintptr_t)avail[j].tail + (uintptr_t)BASE_POINTER))
    {
        avail[j].tail = NULL;
    }

    // used tag and k val is used
    *((u_int32_t *)allocated) = 1;
    *((u_int32_t *)allocated + 1) = j + 7;

    //R3 Part and R4 Part, check if you should split
    while ((1 << (j + 7)) >= 2 * sizeToBeAllocated)
    {
        // if we split to a memory block size,
        // it means that there aren't any free memory blocks of that size
        // if there were any we would have allocated that block
        AvailNode *buddy = (AvailNode *)(((uintptr_t)allocated) + (1 << (j - 1 + 7)));
        ((AvailNode *)(((uintptr_t)buddy) + 8))->next = NULL;
        ((AvailNode *)(((uintptr_t)buddy) + 8))->prev = NULL;

        //tag bit and kval
        *((u_int32_t *)buddy) = 0;
        *((u_int32_t *)buddy + 1) = j - 1 + 7;
        *((u_int32_t *)allocated + 1) -= 1;

        avail[j - 1].head = (AvailNode *)((uintptr_t)buddy - (uintptr_t)BASE_POINTER);
        avail[j - 1].tail = (AvailNode *)((uintptr_t)buddy - (uintptr_t)BASE_POINTER);
        j--;
    }

    sem_post(sem);

    // EXPERIMENT
    /*
    if (EXPERIMENT)
    {
        //experiment_size[findContainingPowerOf2(sizeToBeAllocated) - 7] += sizeToBeAllocated;
        experiment_size[findContainingPowerOf2(sizeToBeAllocated) - 7] += size;
        experiment_count[findContainingPowerOf2(sizeToBeAllocated) - 7] += 1;
    }
    */

    return ((char *)allocated) + 8;
}

// our implementation of the Buddy System Liberation Algorithm,
// from Donald Knuth's The Art Of Computer Programming I
void sbmem_free(void *p)
{
    if (sem_wait(sem) == -1)
    {
        return;
    }

    // correct user offset of memory block
    // we don't expose the first 8 Bytes where we store the use tag and k value
    // of the memory block to the user
    p = (AvailNode *)((uintptr_t)p - 8);
    int k = *(((u_int32_t *)p) + 1);
    while (1)
    {
        void *buddy;

        // find the buddy of the given memory block
        if (((uintptr_t)p - (uintptr_t)BASE_POINTER) % (1 << (k + 1)) == 0)
        {
            //buddy = (AvailNode *)((uintptr_t)p + (1 << k));
            buddy = (void *) ((uintptr_t)p + (1 << k));
        }
        else if (((uintptr_t)p - (uintptr_t)BASE_POINTER) % (1 << (k + 1)) == (1 << k))
        {
            //buddy = (AvailNode *)((uintptr_t)p - (1 << k));
            buddy = (void *) ((uintptr_t)p - (1 << k));
        }
        else
        {
            // REMOVE THIS BEFORE SENDING**********************************************************************************************************************
            // FOR TESTING PURPOSES****************************************************************************************************************************
            // NOT NEEDED IF LIBRARY USED IN THE CORRECT WAY***************************************************************************************************
            printf("REMOVE THIS BEFORE SENDING\n");
        }

        int buddy_tag = *((u_int32_t *)buddy);
        int buddy_k = *(((u_int32_t *)buddy) + 1);

        // no need to check if k == m, as it won't ever happen in our implementation
        // we always have the first block allocated for segment information
        // tag = 1 meaning used, and tag = 0 meaning free
        if ((buddy_tag == 1 || (buddy_tag == 0 && buddy_k != k)))
        {
            break;
        }

        AvailNode *buddyNode = (AvailNode *)(((u_int32_t *)buddy) + 2);

        if (buddyNode->next != NULL)
        {
            // 8 bytes for kval and used tag
            AvailNode *buddyNext = (AvailNode *)((uintptr_t)buddyNode->next + (uintptr_t)BASE_POINTER + 8);
            buddyNext->prev = buddyNode->prev;
        }
        if (buddyNode == (AvailNode *) ((uintptr_t) avail[buddy_k - 7].head + (uintptr_t)BASE_POINTER + 8)) {
            avail[buddy_k - 7].head = buddyNode->prev;
        }

        if (buddyNode->prev != NULL)
        {
            // 8 bytes for kval and used tag
            AvailNode *buddyNext = (AvailNode *)((uintptr_t)buddyNode->prev + (uintptr_t)BASE_POINTER + 8);
            buddyNext->next = buddyNode->next;
        }
        if (buddyNode == (AvailNode *) ((uintptr_t) avail[buddy_k - 7].tail + (uintptr_t)BASE_POINTER + 8)) {
            avail[buddy_k - 7].tail = buddyNode->next;
        }

        buddyNode->prev = NULL;
        buddyNode->next = NULL;

        k = k + 1;

        if (buddy < p)
        {
            p = buddy;
        }
    }

    // used tag and k value
    *((u_int32_t *)p) = 0;
    *(((u_int32_t *)p) + 1) = k;

    // insert the combined block to the available list
    // min 128, 2^7, 7 offset
    // if tail is NULL, the head is also NULL and the list is empty
    if (avail[k - 7].tail == NULL)
    {
        // ****************** Changes in objection start ******************
        AvailNode *node = (AvailNode *)(((u_int32_t *)p) + 2);
        node->prev = NULL;
        node->next = NULL;
        // ****************** Changes in objection end ******************
        avail[k - 7].head = (AvailNode *)((uintptr_t)p - (uintptr_t)BASE_POINTER);
        avail[k - 7].tail = (AvailNode *)((uintptr_t)p - (uintptr_t)BASE_POINTER);
    }
    else
    {
        AvailNode *tailNode = (AvailNode *)((uintptr_t)avail[k - 7].tail + (uintptr_t)BASE_POINTER + 8);
        tailNode->prev = (AvailNode *)((uintptr_t)p - (uintptr_t)BASE_POINTER);
        AvailNode *combinedNode = (AvailNode *)(((u_int32_t *)p) + 2);
        combinedNode->prev = NULL;
        combinedNode->next = avail[k - 7].tail;
        avail[k - 7].tail = (AvailNode *)((uintptr_t) p - (uintptr_t) BASE_POINTER);
    }

    sem_post(sem);
}

int sbmem_close()
{
    if (sem_wait(sem) == -1)
    {
        return -1;
    }

    // decrease process count
    *(((u_int32_t *)BASE_POINTER) + 3) -= 1;

    if (sem_post(sem) == -1)
    {
        return -1;
    }

    return munmap(BASE_POINTER, segmentSize);
}

// EXPERIMENTATION CODE
/*
int init_experiment()
{
    int fd = shm_open(EXP_MEM_NAME, O_RDWR | O_CREAT | O_TRUNC, 0666);

    if (fd == -1)
    {
        return -1;
    }

    if (ftruncate(fd, 4096) == -1)
    {
        return -1;
    }

    void *segment = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    int numMemBlockSizes = findContainingPowerOf2(segmentSize / 128);
    memset(segment, 0, sizeof(int) * 2 * numMemBlockSizes);

    munmap(segment, 4096);

    return 0;
}

int open_experiment()
{
    int fd = shm_open(EXP_MEM_NAME, O_RDWR, 0666);
    int numMemBlockSizes = findContainingPowerOf2(segmentSize / 128);
    experiment_size = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    experiment_count = experiment_size + numMemBlockSizes;

    return 0;
}

int close_experiment()
{
    munmap(experiment_size, 2048);
    return 0;
}

void print_state()
{
    int numMemBlockSizes = findContainingPowerOf2(segmentSize / 128);
    int totalAllocated = 0;
    int totalRequested = 0;
    for (int i = 0; i < numMemBlockSizes; i++) {
        int count = 0;
        //AvailNode *p = (AvailNode *)((uintptr_t)avail[i].head + (uintptr_t)BASE_POINTER + 8);
        uintptr_t p = avail[i].head;
        //for (; p != BASE_POINTER + 8; p = (AvailNode *)((uintptr_t)p->next + (uintptr_t)BASE_POINTER + 8)) {
        // &&  *((u_int32_t *) p) == 0
        for (; p != NULL; p = ((AvailNode *)(p + (uintptr_t)BASE_POINTER + 8))->prev) {
            count++;
        }
        printf("Free block k = %d: %d\n", i + 7, count);
        if (EXPERIMENT) {
            int allocated = (1 << (i + 7)) * experiment_count[i];
            printf("%d B Blocks\t %d B Requested\t %d B Allocated\t %d Allocations\t %.2f Internal Fragmentation\n",
                   (1 << (i + 7)), experiment_size[i], allocated, experiment_count[i], ((double)(allocated - experiment_size[i])) / allocated);
            totalAllocated += (1 << (i + 7)) * experiment_count[i];
            totalRequested += experiment_size[i];
        }
    }

    if (EXPERIMENT) {
        //printf("FREE SPACE: %d\n", segmentSize - totalAllocated);
        //printf("EXTERNAL FRAGMENTATION: %.2f\n", (segmentSize - totalAllocated) / (double)segmentSize);

        //printf("SEGMENT SIZE: %d, TOTAL ALLOCATED: %d, TOTAL REQUESTED: %d, RATIO OF AVERAGE ALLOCATED SPACE TO AVERAGE REQUESTED SPACE (INTERNAL FRAGMENTATION): %.2f \n\n",
        //       segmentSize, totalAllocated, totalRequested, ((double)(totalAllocated) / totalRequested));

        //printf("SEGMENT SIZE: %d, TOTAL ALLOCATED: %d, TOTAL REQUESTED: %d, RATIO OF OVERALLOCATED SPACE TO MEMORY SIZE (INTERNAL FRAGMENTATION): %.2f \n\n",
        //       segmentSize, totalAllocated, totalRequested, ((double)(totalAllocated - totalRequested) / segmentSize));
        // avg allocated, overallocated, external
        
        printf("%.2f %.2f %.3f\n", ((double)(totalAllocated) / totalRequested), ((double)(totalAllocated - totalRequested) / segmentSize), (segmentSize - totalAllocated) / (double)segmentSize);
    }
}

int get_free_space() {
    int numMemBlockSizes = findContainingPowerOf2(segmentSize / 128);
    int totalAllocated = 0;
    for (int i = 0; i < numMemBlockSizes; i++) {
        if (EXPERIMENT) {
            int allocated = (1 << (i + 7)) * experiment_count[i];
            //printf("%d B Blocks\t %d B Requested\t %d B Allocated\t %d Allocations\t %.2f Internal Fragmentation\n",
            //       (1 << (i + 7)), experiment_size[i], allocated, experiment_count[i], ((double)(allocated - experiment_size[i])) / allocated);
            totalAllocated += (1 << (i + 7)) * experiment_count[i];
        }
    }
    
    return segmentSize - totalAllocated;
}
*/