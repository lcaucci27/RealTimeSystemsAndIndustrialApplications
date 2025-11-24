// RPU Coherence Test - TODO
#include <stdio.h>
#include "xil_cache.h"

#define SHARED_MEM_BASE 0x3e000000  // Base address for the shared memory region
#define MAGIC_VALUE 0xCAFEBABE      // Value we're expecting from the APU

// Pointer to shared memory - volatile so the compiler doesn't optimize reads/writes
volatile uint32_t *shared_mem = (uint32_t*)SHARED_MEM_BASE;

int main() {
    printf("RPU: Starting coherence test\n");
    
    // Enable the data cache if needed
    Xil_DCacheEnable();
    
    // Main loop: wait for data coming from the APU
    while(1) {
        // Read the value from shared memory
        uint32_t value = *shared_mem;
        
        // Check if we got the magic value we were waiting for
        if(value == MAGIC_VALUE) {
            printf("RPU: Received correct value!\n");
            
            // Write our response back to the APU
            shared_mem[1] = 0xDEADBEEF;
            
            // Reset the first location so we can do another test
            *shared_mem = 0;
        }
        
        // Add a small delay to avoid hammering the memory bus
        for(volatile int i=0; i<100000; i++);
    }
    
    return 0;
}