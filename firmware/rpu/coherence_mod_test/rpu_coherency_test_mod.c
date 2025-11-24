/* rpu_stress_reader.c */
#include <stdio.h>
#include <stdint.h>
#include "xil_printf.h"

#define SHARED_MEM 0x18A0000UL  // Make sure this matches what your module outputs
#define PATTERN_OLD 0x0F0F0F0F  // The old value that's sitting in DDR
#define PATTERN_NEW 0xF0F0F0F0  // The new value the APU writes to its cache
#define NUM_READS 100000        // How many times we'll read from memory

int main(void)
{
    // Pointer to the shared memory location - volatile so reads aren't optimized away
    volatile uint32_t *mem = (volatile uint32_t *)SHARED_MEM;
    uint32_t val;
    uint32_t old_count = 0;   // Count how many times we see the old pattern
    uint32_t new_count = 0;   // Count how many times we see the new pattern
    uint32_t other_count = 0; // Count anything else (shouldn't happen but just in case)

    xil_printf("\r\n=== RPU Stress Test Reader ===\r\n");
    xil_printf("Reading from 0x%08lX\r\n", SHARED_MEM);
    xil_printf("Performing %d reads...\r\n\r\n", NUM_READS);

    // Main loop: read the same location over and over
    for (uint32_t i = 0; i < NUM_READS; i++) {
        val = mem[0];  // Read from shared memory

        // Check what value we got
        if (val == PATTERN_OLD) {
            old_count++;
        } else if (val == PATTERN_NEW) {
            new_count++;
        } else {
            other_count++;
        }

        // Print progress every 10k reads so we know it's still running
        if (i % 10000 == 0 && i > 0) {
            xil_printf("Progress: %lu reads\r\n", i);
        }
    }

    // Print out the results
    xil_printf("\r\n===========================================\r\n");
    xil_printf("RESULTS after %d reads:\r\n", NUM_READS);
    xil_printf("===========================================\r\n");
    xil_printf("OLD (0x0F0F0F0F): %lu (%.1f%%)\r\n",
               old_count, (old_count*100.0)/NUM_READS);
    xil_printf("NEW (0xF0F0F0F0): %lu (%.1f%%)\r\n",
               new_count, (new_count*100.0)/NUM_READS);
    xil_printf("Other:            %lu (%.1f%%)\r\n",
               other_count, (other_count*100.0)/NUM_READS);
    xil_printf("===========================================\r\n\r\n");

    // Check if we ever saw the new pattern (meaning coherency is working)
    if (new_count > 0) {
        xil_printf("SUCCESS! RPU read NEW pattern from APU cache!\r\n");
        xil_printf("Cache coherency via CCI-400 is WORKING!\r\n");
        xil_printf("\r\nCoherency rate: %.1f%%\r\n", (new_count*100.0)/(old_count+new_count));
    } else {
        xil_printf("NO COHERENCY detected\r\n");
        xil_printf("All reads came from DDR (OLD pattern)\r\n");
    }

    xil_printf("\r\nTest complete.\r\n");

    while(1);  // Just hang here when we're done
    return 0;
}