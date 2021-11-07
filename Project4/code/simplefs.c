#include "simplefs.h"
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
    Mustafa Göktan Güdükbay 21801740
    Musa Ege Ünalan         21803617
*/


// Global Variables =======================================
int vdisk_fd; // Global virtual disk file descriptor. Global within the library.
              // Will be assigned with the vsfs_mount call.
              // Any function in this file can use this.
              // Applications will not use  this directly.
// ========================================================
int openFileTable[] = {-1, -1, -1, -1, -1, -1, -1, -1,
                       -1, -1, -1, -1, -1, -1, -1, -1}; //directory entry index is file descriptor

struct directoryEntry {
    char fileName[110];
    int isUsed;
    int fcb;
    char padding[8];
};

struct fcb {
    int size;
    int index; //index file
    int isUsed;
    int permission;
    int readPointer;  //bytes read currently
    int writePointer; //bytes appended currently
    char padding[104];
};

// read block k from disk (virtual disk) into buffer block.
// size of the block is BLOCKSIZE.
// space for block must be allocated outside of this function.
// block numbers start from 0 in the virtual disk.
int read_block(void *block, int k) {
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vdisk_fd, (off_t)offset, SEEK_SET);
    n = read(vdisk_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
        printf("read error\n");
        return -1;
    }
    return (0);
}

// write block k into the virtual disk.
int write_block(void *block, int k) {
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vdisk_fd, (off_t)offset, SEEK_SET);
    n = write(vdisk_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
        printf("write error\n");
        return (-1);
    }
    return 0;
}

/**********************************************************************
   The following functions are to be called by applications directly.
***********************************************************************/

// this function is partially implemented.
int create_format_vdisk(char *vdiskname, unsigned int m) {
    char command[1000];
    int size;
    int num = 1;
    int count;
    size = num << m;          //m 17
    count = size / BLOCKSIZE; //2^12
    //    printf ("%d %d", m, size);
    sprintf(command, "dd if=/dev/zero of=%s bs=%d count=%d",
            vdiskname, BLOCKSIZE, count);
    system(command);

    // now write the code to format the disk below.
    sfs_mount(vdiskname);

    //super block to block 0
    int *superblock = malloc(BLOCKSIZE);
    memset(superblock, 0, BLOCKSIZE);
    //number of blocks
    superblock[0] = count;

    //number of blocks used
    superblock[1] = 13;

    write_block(superblock, 0);

    free(superblock);

    //bitmaps
    void *init = malloc(BLOCKSIZE);
    memset(init, 0, BLOCKSIZE);

    // initialize blocks 2-12 to all 0 bytes
    for (int i = 2; i <= 12; i++) {
        write_block(init, i);
    }
    // set first 13 (0 to 12) bits of the bitmap to 1 = used
    //*(u_int16_t*) init = 0b1111111111111000;
    // 0b11111111111110000000000000000000 32 bits
    *(u_int32_t*) init = 4294443008;
    write_block(init, 1);

    free(init);

    sfs_umount();

    return (0);
}

// already implemented
int sfs_mount(char *vdiskname) {
    // simply open the Linux file vdiskname and in this
    // way make it ready to be used for other operations.
    // vdisk_fd is global; hence other function can use it.
    vdisk_fd = open(vdiskname, O_RDWR);
    return (0);
}

// already implemented
int sfs_umount() {
    fsync(vdisk_fd); // copy everything in memory to disk
    close(vdisk_fd);
    return (0);
}

int get_free_blocks(int* blocks, int count) {
    int *superInformation = malloc(BLOCKSIZE);
    read_block(superInformation, 0);
    u_int32_t *bitmap_block = malloc(BLOCKSIZE);
    if (count <= superInformation[0] - superInformation[1]) {
        int found = 0;
        for (int i = 0; i < 4 && found < count; i++) {
            read_block(bitmap_block, 1+i);
            for (int j = 0; j < BLOCKSIZE * 8 && found < count; j++) {
                // check if the most significant bit is 0
                if ((bitmap_block[j / 32] << (j % 32)) < (((u_int32_t) 1) << 31)) {
                    blocks[found] = (i * BLOCKSIZE * 8) + j;
                    bitmap_block[j / 32] += ((u_int32_t) 1) << (31 - (j % 32));
                    found++;
                }
            }
            write_block(bitmap_block, 1+i);
        }
        superInformation[1] += count;
        write_block(superInformation, 0);
        free(superInformation);
        free(bitmap_block);
        return 0;
    }
    else {
        free(superInformation);
        free(bitmap_block);
        return -1;
    }
}

int sfs_create(char *filename) {
    // find fcb entry
    int fcb_block_number = 8;
    int fcb_index = -1;
    struct fcb *fcb_block = malloc(BLOCKSIZE);
    for (int i = 0; i < 128; i++) {
        if (i % 32 == 0) {
            fcb_block_number++;
            read_block(fcb_block, fcb_block_number);
        }

        if (fcb_block[i % 32].isUsed == 0) {
            fcb_index = i;
            fcb_block[i % 32].size = 0;
            fcb_block[i % 32].isUsed = 1;
            fcb_block[i % 32].readPointer = 0;
            fcb_block[i % 32].writePointer = 0;
            fcb_block[i % 32].permission = 0;
            break;
        }
    }

    if (fcb_index == -1) {
        free(fcb_block);
        return -1;
    }


    int* blocks = malloc(2 * sizeof *blocks);
    if (get_free_blocks(blocks, 2) == -1) {
        free(blocks);
        free(fcb_block);
        return -1;
    }

    fcb_block[fcb_index % 32].index = blocks[1];
    write_block(fcb_block, fcb_block_number);

    //directory entry
    int directory_block_number = 4;
    struct directoryEntry *directory_entry = malloc(BLOCKSIZE);
    for (int i = 0; i < 128; i++) {
        if (i % 32 == 0) {
            directory_block_number++;
            read_block(directory_entry, directory_block_number);
        }

        if (directory_entry[i % 32].isUsed == 0) {
            strcpy(directory_entry[i % 32].fileName, filename);
            directory_entry[i % 32].isUsed = 1;
            directory_entry[i % 32].fcb = fcb_index;
            write_block(directory_entry, i / 32 + 5);
            break;
        }
    }

    //write to the index file
    int *indexBlock = malloc(BLOCKSIZE);
    read_block(indexBlock, blocks[1]);

    indexBlock[0] = blocks[0];
    for (int i = 1; i < 1024; i++)
        indexBlock[i] = -1;

    write_block(indexBlock, blocks[1]);

    free(fcb_block);
    free(directory_entry);
    free(blocks);
    free(indexBlock);

    return (0);
}

int sfs_open(char *file, int mode) {
    //check if there are 16 open files
    int freeOpenIndex = -1;
    for (int k = 0; k < 16; k++) {
        if (openFileTable[k] == -1) {
            freeOpenIndex = k;
            break;
        }
    }

    if (freeOpenIndex == -1)
        return -1;

    //find file directory corresponding to the file
    int directory_index = -1;
    int directory_block_number = 5;
    struct directoryEntry *directory_entry = malloc(BLOCKSIZE);
    for (int i = 0; i < 128; i++) {
        if (i % 32 == 0) {
            read_block(directory_entry, directory_block_number);
            directory_block_number++;
        }

        if (strcmp(directory_entry[i % 32].fileName, file) == 0) {
            directory_index = i;
            break;
        }
    }

    if (directory_index == -1) {
        free(directory_entry);
        return -1; //no file with the given name
    }

    openFileTable[freeOpenIndex] = directory_index;

    int fcbIndex = directory_entry[directory_index % 32].fcb;

    //get the fcb entry
    struct fcb *fcb_block = malloc(BLOCKSIZE);
    read_block(fcb_block, fcbIndex / 32 + 9);

    fcb_block[fcbIndex % 32].permission = mode;

    write_block(fcb_block, fcbIndex / 32 + 9);

    read_block(fcb_block, fcbIndex / 32 + 9);

    free(directory_entry);
    free(fcb_block);

    return freeOpenIndex; //file descriptor
}

int sfs_close(int fd) {
    for (int i = 0; i < 16; i++) {
        if (fd == openFileTable[i]) {
            openFileTable[i] = -1;
            return 0;
        }
    }

    return -1;
}

int sfs_getsize(int fd) {
    int directory_index = openFileTable[fd];
    if (directory_index == -1)
        return -1;

    struct directoryEntry *directories = malloc(BLOCKSIZE);
    read_block(directories, directory_index / 32 + 5);

    int fcb_index = directories[directory_index % 32].fcb;
    free(directories);

    struct fcb *fcbs = malloc(BLOCKSIZE);
    read_block(fcbs, fcb_index / 32 + 9);

    int size = fcbs[fcb_index % 32].size;
    
    free(fcbs);

    return size;
}

int sfs_read(int fd, void *buf, int n) {
    //get the file directory from the openFileTable
    int directory_index = openFileTable[fd];

    if (directory_index == -1)
        return -1;

    //get the fcb from the directory entry and check the mode
    struct directoryEntry *directory_entry = malloc(BLOCKSIZE);
    read_block(directory_entry, (directory_index / 32) + 5);
    int fcbIndex = directory_entry[directory_index % 32].fcb;

    struct fcb *fcb = malloc(BLOCKSIZE);
    read_block(fcb, (fcbIndex / 32) + 9);

    int permission = fcb[fcbIndex % 32].permission;

    if (permission == 1) { //no read permission
        free(directory_entry);
        free(fcb);
        return -1;
    }

    int read_start = fcb[fcbIndex % 32].readPointer;
    int bytesRead = 0;
    int *indexBlock = malloc(BLOCKSIZE);
    read_block(indexBlock, fcb[fcbIndex % 32].index);
    int file_data_index;
    char *data = malloc(BLOCKSIZE);
    while (n > bytesRead) {
        file_data_index = indexBlock[read_start / BLOCKSIZE];
        //printf("FILE DATA INDEX: %d\n", file_data_index);
        read_block(data, file_data_index);
        if ((n - bytesRead) <= (BLOCKSIZE - read_start % BLOCKSIZE)) {
            memcpy(((char *) buf) + bytesRead, data + (read_start % BLOCKSIZE), n - bytesRead);
            read_start += n - bytesRead;
            bytesRead = n;
        }
        else {
            memcpy(((char *) buf) + bytesRead, data + (read_start % BLOCKSIZE), BLOCKSIZE - (read_start % BLOCKSIZE));
            bytesRead += BLOCKSIZE - (read_start % BLOCKSIZE);
            //read_start = 0;
            read_start += BLOCKSIZE - (read_start % BLOCKSIZE);
        }
    }
    fcb[fcbIndex % 32].readPointer = read_start;
    write_block(fcb, (fcbIndex / 32) + 9);

    free(indexBlock);
    free(data);
    free(directory_entry);
    free(fcb);

    return bytesRead;
}

int sfs_append(int fd, void *buf, int n) {
    //check if the file is file is file is opened
    int directory_index = openFileTable[fd];

    if (directory_index == -1) {
        return -1;
    }

    //get the fcb from the directory entry and check the mode
    struct directoryEntry *directory_entry = malloc(BLOCKSIZE);
    read_block(directory_entry, (directory_index / 32) + 5);

    int fcbIndex = directory_entry[directory_index % 32].fcb;

    struct fcb *fcb = malloc(BLOCKSIZE);
    read_block(fcb, (fcbIndex / 32) + 9);

    int permission = fcb[fcbIndex % 32].permission;

    if (permission == 0) { //no write permission
        free(directory_entry);
        free(fcb);
        return -1;
    }

    //check if n + size exceeds 4MB
    if (n + fcb[fcbIndex % 32].size > (1 << 22)) {
        return 0;
    }

    //write to the file, get the write start bytes
    int write_start = fcb[fcbIndex % 32].writePointer;

    //get the index block
    int *indexBlock = malloc(BLOCKSIZE);
    read_block(indexBlock, fcb[fcbIndex % 32].index);

    //check if you can allocate space if needed in the superblock
    int *superblock = malloc(BLOCKSIZE);
    read_block(superblock, 0);
    int extraBlocksNeeded = 0;

    if (write_start % BLOCKSIZE == 0 && write_start != 0) {
        extraBlocksNeeded = n / BLOCKSIZE;
        if (n % BLOCKSIZE != 0) {
            extraBlocksNeeded += 1;
        }
    }
    else {
        if ((write_start + n) % BLOCKSIZE == 0) {
            extraBlocksNeeded = ((write_start + n) / BLOCKSIZE) - ((write_start / BLOCKSIZE) + 1);
        }
        else {
            extraBlocksNeeded = ((write_start + n) / BLOCKSIZE) - (write_start / BLOCKSIZE);
        }
    }

    if (extraBlocksNeeded > (superblock[0] - superblock[1])) {
        free(indexBlock);
        free(superblock);
        free(directory_entry);
        free(fcb);
        return -1;
    }

    if (extraBlocksNeeded > 0) {
        int* blocks = malloc(extraBlocksNeeded * sizeof *blocks);
        if (get_free_blocks(blocks, extraBlocksNeeded) == -1) {
            free(indexBlock);
            free(superblock);
            free(directory_entry);
            free(fcb);
            return -1;
        }
        int lastIndex;
        if (write_start % BLOCKSIZE == 0 && write_start != 0) {
            lastIndex = (write_start / BLOCKSIZE) - 1;
        }
        else {
            lastIndex = (write_start / BLOCKSIZE);
        }

        for (int i = 0; i < extraBlocksNeeded; i++) {
            lastIndex++;
            indexBlock[lastIndex] = blocks[i];
        }
        write_block(indexBlock, fcb[fcbIndex % 32].index);
        free(blocks);
    }

    char *data = malloc(BLOCKSIZE);
    int bytesWritten = 0;

    int file_data_index;

    while (n > bytesWritten) {
        //get the starting block data from the index file
        file_data_index = indexBlock[write_start / BLOCKSIZE];
        //printf("FILE DATA INDEX: %d\n", file_data_index);
        read_block(data, file_data_index);
        if ((n - bytesWritten) <= (BLOCKSIZE - (write_start % BLOCKSIZE))) {
            memcpy(data + (write_start % BLOCKSIZE), buf + bytesWritten, n - bytesWritten);
            write_start += n - bytesWritten;
            bytesWritten = n;
        }
        else {
            memcpy(data + (write_start % BLOCKSIZE), ((char *) buf) + bytesWritten, BLOCKSIZE - (write_start % BLOCKSIZE));
            bytesWritten += BLOCKSIZE - (write_start % BLOCKSIZE);
            write_start += BLOCKSIZE - (write_start % BLOCKSIZE);
        }

        write_block(data, file_data_index);
    }

    fcb[fcbIndex % 32].size += n;
    fcb[fcbIndex % 32].writePointer = write_start;

    write_block(fcb, (fcbIndex / 32) + 9); //update size and write pointer

    free(data);
    free(directory_entry);
    free(fcb);
    free(superblock);
    free(indexBlock);

    return bytesWritten;
}

int sfs_delete(char *filename) {
    int directoryIndex = -1;
    int directoryBlock = 4;
    struct directoryEntry *directories = malloc(BLOCKSIZE);
    for (int i = 0; i < 128; i++) {
        if (i % 32 == 0) {
            directoryBlock++;
            read_block(directories, directoryBlock);
        }
        if (strcmp(directories[i%32].fileName, filename) == 0 && directories[i%32].isUsed) { //directory entry found
            directoryIndex = i;
            break;
        }
    }

    if (directoryIndex == -1) {
        //file not found
        free(directories);
        return -1;
    } 

    //mark directory entry as free
    directories[directoryIndex % 32].isUsed = 0;

    //get fcb
    int fcbIndex = directories[directoryIndex%32].fcb;

    write_block(directories, directoryBlock);
    free(directories);

    struct fcb *fcbs = malloc(BLOCKSIZE);
    read_block(fcbs, fcbIndex / 32 + 9);
    fcbs[fcbIndex % 32].isUsed = 0; //mark fcb as free

    write_block(fcbs, fcbIndex / 32 + 9);

    int *indexBlock = malloc(BLOCKSIZE);
    read_block(indexBlock, fcbs[fcbIndex % 32].index);
    u_int32_t *bitmap_block = malloc(BLOCKSIZE);
    for (int i = 0; i < (fcbs[fcbIndex % 32].size / BLOCKSIZE) + 1; i++) {
        // can be optimized to not read again if it is the same block
        int currBlock = indexBlock[i] / (BLOCKSIZE * 8);
        read_block(bitmap_block, 1 + currBlock);
        bitmap_block[(indexBlock[i] % (BLOCKSIZE * 8)) / 32] -=
            ((u_int32_t)1) << (31 - ((indexBlock[i] % (BLOCKSIZE * 8)) % 32));
        write_block(bitmap_block, 1 + currBlock);
    }

    int currBlock = fcbs[fcbIndex % 32].index / (BLOCKSIZE * 8);
    read_block(bitmap_block, 1 + currBlock);
    bitmap_block[(fcbs[fcbIndex % 32].index % (BLOCKSIZE * 8)) / 32] -=
        ((u_int32_t)1) << (31 - ((fcbs[fcbIndex % 32].index % (BLOCKSIZE * 8)) % 32));
    write_block(bitmap_block, 1 + currBlock);

    int *superInformation = malloc(BLOCKSIZE);
    read_block(superInformation, 0);
    int size = fcbs[fcbIndex % 32].size;
    if (size % BLOCKSIZE == 0 && size != 0) {
        superInformation[1] -= 1 + (size / BLOCKSIZE);
    }
    else {
        superInformation[1] -= 2 + (size / BLOCKSIZE);
    }
    write_block(superInformation, 0);

    free(superInformation);
    free(fcbs);
    free(bitmap_block);
    free(indexBlock);

    return (0);
}