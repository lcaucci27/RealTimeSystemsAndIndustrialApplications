#include <stdio.h>
#include "xil_printf.h"
#include "xil_cache.h"
#include "xil_mmu.h"
#include "xparameters.h"
#include "sleep.h"

// Shared buffer address (MUST match RPU side)
#define SHARED_BUFFER_ADDR 0x70000000
#define BUFFER_SIZE 256

// Test patterns
#define PATTERN_OLD 0x0F0F0F0F
#define PATTERN_NEW 0xF0F0F0F0

// MMU definitions: Outer Shareable, Write-Back Cacheable
#define NORM_WB_OUT_CACHE (0x000000FFU | (0x2 << 8)) // Standard: MT_NORMAL_WB | OUTER_SHAREABLE = 0x2FF

int main(void)
{
	volatile u32 *shared_buffer = (volatile u32 *)SHARED_BUFFER_ADDR;
	int i;

	xil_printf("\n\r========================================\n\r");
	xil_printf("APU Coherency Test Application\n\r");
	xil_printf("========================================\n\r");
	xil_printf("Shared Buffer Address: 0x%08x\n\r", SHARED_BUFFER_ADDR);

	// CRITICAL SETUP: Set memory region as outer shareable
	xil_printf("Configuring MMU for outer shareable...\n\r");
	//Xil_SetTlbAttributes((UINTPTR)shared_buffer, NORM_WB_OUT_CACHE);
	//dsb(); // Data Synchronization Barrier
	xil_printf(" MMU configured\n\r");

	// PHASE 1: Write old pattern and sync with DDR
	xil_printf("\n\rPHASE 1: Writing OLD pattern...\n\r");
	for (i = 0; i < BUFFER_SIZE; i++) {
		shared_buffer[i] = PATTERN_OLD;
	}

	// Flush cache to actually write to DDR
	xil_printf("Flushing cache to DDR...\n\r");
	Xil_DCacheFlushRange(SHARED_BUFFER_ADDR, BUFFER_SIZE * sizeof(u32));
	xil_printf(" DDR now contains: 0x%08x\n\r", PATTERN_OLD);

	sleep(1);

	// PHASE 2: Write new pattern WITHOUT flushing
	xil_printf("\n\rPHASE 2: Writing NEW pattern...\n\r");
	for (i = 0; i < BUFFER_SIZE; i++) {
		shared_buffer[i] = PATTERN_NEW;
	}

	// CRITICAL: Do NOT flush cache here!
	xil_printf("IMPORTANT: Cache NOT flushed!\n\r");
	xil_printf(" APU cache contains: 0x%08x\n\r", PATTERN_NEW);
	xil_printf(" DDR still contains: 0x%08x\n\r", PATTERN_OLD);

	// PHASE 3: Inform user what's happening
	xil_printf("\n\r========================================\n\r");
	xil_printf("Setup complete!\n\r");
	xil_printf("Current situation:\n\r");
	xil_printf(" - APU cache: 0x%08x (NEW)\n\r", PATTERN_NEW);
	xil_printf(" - DDR: 0x%08x (OLD)\n\r", PATTERN_OLD);
	xil_printf("\n\rIF coherence is WORKING:\n\r");
	xil_printf(" RPU will read 0x%08x from APU cache\n\r", PATTERN_NEW);
	xil_printf("\n\rIF coherence is NOT working:\n\r");
	xil_printf(" RPU will read 0x%08x from DDR\n\r", PATTERN_OLD);
	xil_printf("========================================\n\r");

	xil_printf("\n\rNow start the RPU application to check!\n\r");

	// Continuous update loop
	xil_printf("\n\rContinuously updating buffer...\n\r");
	u32 counter = 0;
	while(1) {
		// Update buffer periodically (without flush)
		shared_buffer[0] = PATTERN_NEW + counter;
		xil_printf("Buffer[0] updated to: 0x%08x (cache only)\r", PATTERN_NEW + counter);
		counter++;
		sleep(2);
	}

	return 0;
}