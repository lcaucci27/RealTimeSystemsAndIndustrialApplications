#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define SHARED_MEM_BASE 0x3e000000  // Base address of our shared memory region
#define SHARED_MEM_SIZE 0x00800000  // Size of the region we want to map
#define MAGIC_VALUE 0xF0F0F0F0      // Test value we'll send to the RPU

int main() {
    // Open /dev/mem to access physical memory directly
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if(fd < 0) {
        perror("open /dev/mem");
        return 1;
    }
    
    // Map the shared memory region into our process space
    volatile uint32_t *shared = mmap(NULL, SHARED_MEM_SIZE,
        PROT_READ | PROT_WRITE, MAP_SHARED,
        fd, SHARED_MEM_BASE);
    
    if(shared == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    
    printf("APU: Starting coherence test\n");
    
    // Write the magic value to shared memory so the RPU can see it
    printf("APU: Writing 0x%08X\n", MAGIC_VALUE);
    shared[0] = MAGIC_VALUE;
    
    // Now wait for the RPU to write back its response
    printf("APU: Waiting for RPU response...\n");
    int timeout = 100;  // Give it up to 10 seconds
    while(shared[1] != 0xDEADBEEF && timeout-- > 0) {
        usleep(100000); // Sleep for 100ms between checks
    }
    
    // Check if we got the expected response
    if(shared[1] == 0xDEADBEEF) {
        printf("SUCCESS: Coherence working!\n");
    } else {
        printf("FAIL: No response from RPU\n");
    }
    
    // Clean up: unmap memory and close the file descriptor
    munmap((void*)shared, SHARED_MEM_SIZE);
    close(fd);
    return 0;
}