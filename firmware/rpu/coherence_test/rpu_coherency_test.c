#include <stdio.h>
#include "xil_cache.h"

#define SHARED_MEM_BASE 0x3e000000  // Where we set up shared memory in the device tree
#define MAGIC_VALUE 0xCAFEBABE      // What we're expecting APU to write

// Volatile so compiler doesn't mess with our reads/writes
volatile uint32_t *shared_mem = (uint32_t*)SHARED_MEM_BASE;

int main() {
    printf("RPU: Starting coherence test\n");
    
    // Turn on data cache if it's not already
    Xil_DCacheEnable();
    
    // Just keep polling for data from APU
    while(1) {
        // Grab whatever's at the shared memory location
        uint32_t value = *shared_mem;
        
        // Did we get what we were waiting for?
        if(value == MAGIC_VALUE) {
            printf("RPU: Received correct value!\n");
            
            // Write our response back
            shared_mem[1] = 0xDEADBEEF;
            
            // Clear the first slot so we can loop again
            *shared_mem = 0;
        }
        
        // Small delay so we don't absolutely hammer the bus
        for(volatile int i=0; i<100000; i++);
    }
    
    return 0;
}