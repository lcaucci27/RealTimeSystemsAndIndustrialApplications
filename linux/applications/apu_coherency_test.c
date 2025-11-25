#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define SHARED_MEM_BASE 0x3e000000  // Physical address of our shared memory region
#define SHARED_MEM_SIZE 0x00800000  // How much we're mapping (8MB)
#define MAGIC_VALUE 0xF0F0F0F0      // Test value to send to RPU

int main() {
    // Need to open /dev/mem to get at physical memory
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if(fd < 0) {
        perror("open /dev/mem");
        return 1;
    }
    
    // Map shared memory region into our address space
    volatile uint32_t *shared = mmap(NULL, SHARED_MEM_SIZE,
        PROT_READ | PROT_WRITE, MAP_SHARED,
        fd, SHARED_MEM_BASE);
    
    if(shared == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    
    printf("APU: Starting coherence test\n");
    
    // Write magic value so RPU can pick it up
    printf("APU: Writing 0x%08X\n", MAGIC_VALUE);
    shared[0] = MAGIC_VALUE;
    
    // Wait for RPU to write back its response
    printf("APU: Waiting for RPU response...\n");
    int timeout = 100;  // Give it up to 10 seconds
    while(shared[1] != 0xDEADBEEF && timeout-- > 0) {
        usleep(100000); // Check every 100ms
    }
    
    // See if we got what we expected
    if(shared[1] == 0xDEADBEEF) {
        printf("SUCCESS: Coherence working!\n");
    } else {
        printf("FAIL: No response from RPU\n");
    }
    
    // Cleanup
    munmap((void*)shared, SHARED_MEM_SIZE);
    close(fd);
    return 0;
}