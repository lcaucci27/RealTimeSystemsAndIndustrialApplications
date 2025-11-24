/*
 * RPU Receiver - Cache Invalidation Overhead Measurement
 * 
 * This version is specifically designed to measure ONLY the cache invalidation overhead.
 * Basically, we're trying to figure out what cost the CCI-400 hardware coherence would save us.
 * 
 * Timer: TTC0 Timer 0 at 0xFF110000 (~100 MHz)
 * Shared Memory: 0x3E000000
 */

#include <stdint.h>
#include <string.h>
#include "xil_printf.h"
#include "xil_cache.h"
#include "xil_io.h"

/* Shared Memory Configuration */
#define SHARED_MEM_BASE     0x3E000000UL
#define SHARED_MEM_SIZE     0x00800000UL  /* 8 MB */
#define CACHE_LINE_SIZE     64  /* ARM cache line size */

/* Protocol Magic Values */
#define MAGIC_START         0x0F0F0F0FUL
#define MAGIC_ACK           0xF0F0F0F0UL
#define MAGIC_DONE          0xFFFFFFFFUL
#define MAGIC_READY         0xAAAAAAAAUL

/* TTC0 Timer 0 Registers */
#define TTC0_BASE           0xFF110000UL
#define TTC0_CLK_CTRL       (TTC0_BASE + 0x00)
#define TTC0_CNT_CTRL       (TTC0_BASE + 0x0C)
#define TTC0_CNT_VAL        (TTC0_BASE + 0x18)

/* Timer frequency */
#define TIMER_FREQ_HZ       100000000UL
#define TIMER_FREQ_MHZ      100.0

/* Results storage */
#define RESULTS_OFFSET      0x00400000UL
#define MAX_RESULTS         10000

/* Shared memory pointers */
volatile uint32_t *shared_mem = (volatile uint32_t *)SHARED_MEM_BASE;
volatile uint32_t *results_mem = (volatile uint32_t *)(SHARED_MEM_BASE + RESULTS_OFFSET);

/* Result structure */
typedef struct {
    uint32_t packet_size;
    uint32_t apu_timestamp;
    uint32_t rpu_timestamp;
    uint32_t delta_ticks;
    uint32_t valid;
} __attribute__((packed)) result_entry_t;

/* Global variables */
static uint32_t result_count = 0;

/**
 * Initialize TTC0 Timer 0
 */
static void init_timer(void)
{
    xil_printf("RPU: Initializing TTC0 Timer 0...\r\n");
    
    // First, stop the counter
    Xil_Out32(TTC0_CNT_CTRL, 0x01);
    
    // Set up the clock with no prescaler
    Xil_Out32(TTC0_CLK_CTRL, 0x00);
    
    // Now start it up
    Xil_Out32(TTC0_CNT_CTRL, 0x00);
    
    // Quick sanity check to make sure it's actually running
    uint32_t val1 = Xil_In32(TTC0_CNT_VAL);
    for (volatile int i = 0; i < 1000; i++);
    uint32_t val2 = Xil_In32(TTC0_CNT_VAL);
    
    if (val2 != val1) {
        xil_printf("RPU: TTC0 Timer running!\r\n");
    } else {
        xil_printf("RPU: WARNING - Timer not running!\r\n");
    }
}

/**
 * Read timer
 */
static inline uint32_t read_timer(void)
{
    return Xil_In32(TTC0_CNT_VAL);
}

/**
 * Invalidate ONLY the control word (first cache line)
 */
static inline void invalidate_control_word(void)
{
    Xil_DCacheInvalidateRange((INTPTR)shared_mem, CACHE_LINE_SIZE);
}

/**
 * Flush only the control word
 */
static inline void flush_control_word(void)
{
    Xil_DCacheFlushRange((INTPTR)shared_mem, CACHE_LINE_SIZE);
}

/**
 * Flush results area
 */
static inline void flush_results(void)
{
    uint32_t bytes_to_flush = 4 + (result_count * 20);
    Xil_DCacheFlushRange((INTPTR)results_mem, bytes_to_flush);
}

/**
 * Store result
 */
static void store_result(uint32_t pkt_size, uint32_t apu_ts, uint32_t rpu_ts)
{
    if (result_count >= MAX_RESULTS) return;
    
    // Calculate the time difference, handling counter wraparound
    uint32_t delta;
    if (rpu_ts >= apu_ts) {
        delta = rpu_ts - apu_ts;
    } else {
        delta = (0xFFFFFFFF - apu_ts) + rpu_ts + 1;
    }
    
    // Store everything in the results buffer
    uint32_t offset = 1 + (result_count * 5);
    results_mem[offset + 0] = pkt_size;
    results_mem[offset + 1] = apu_ts;
    results_mem[offset + 2] = rpu_ts;
    results_mem[offset + 3] = delta;
    results_mem[offset + 4] = 0xA5A5A5A5UL;  // Validation marker
    
    result_count++;
}

/**
 * Main receiver loop - MEASURES ONLY CACHE INVALIDATION OVERHEAD
 * 
 * Okay, so here's what we're measuring:
 * 1. Time for APU to write data to DRAM (it uses O_SYNC so it's uncached)
 * 2. Time for RPU to detect the signal by polling
 * 3. Time for RPU to invalidate cache for the metadata
 * 4. Time for RPU to invalidate cache for the actual payload
 * 
 * What we're NOT measuring is the time to actually read and process the data,
 * because that cost would be there whether or not we have cache coherence.
 * 
 * The cache invalidation operations are the key thing - this is what CCI-400
 * hardware coherence would completely eliminate.
 */
static void receiver_loop(void)
{
    uint32_t rpu_ts, apu_ts, packet_size;
    uint32_t packets_received = 0;
    
    xil_printf("RPU: Entering receiver loop (INVALIDATION OVERHEAD ONLY)...\r\n");
    xil_printf("RPU: Waiting for packets at 0x%08X\r\n", (uint32_t)shared_mem);
    
    // Tell the APU we're ready to go
    shared_mem[0] = MAGIC_READY;
    flush_control_word();
    
    while (1) {
        // We only need to invalidate the control word for polling
        invalidate_control_word();
        
        // Check if APU told us we're done
        if (shared_mem[0] == MAGIC_DONE) {
            xil_printf("RPU: Received DONE signal\r\n");
            break;
        }
        
        // Check if there's a new packet waiting
        if (shared_mem[0] == MAGIC_START) {
            /* 
             * Here's where we measure the real overhead that CCI-400 would save:
             * - Invalidate the metadata cache lines
             * - Read the metadata (size and timestamp)
             * - Invalidate the payload cache lines
             * 
             * Note that we DON'T actually read through the payload data byte-by-byte.
             * That's intentional - reading the data would take time regardless of
             * whether we have hardware coherence or not. We just want the cache overhead.
             */
            
            // Invalidate metadata (first 256 bytes = 4 cache lines)
            Xil_DCacheInvalidateRange((INTPTR)shared_mem, 256);
            
            // Read the metadata we need
            packet_size = shared_mem[1];
            apu_ts = shared_mem[2];
            
            /* 
             * This is the really important part: invalidate the payload cache lines.
             * This is literally the overhead that CCI-400 would get rid of!
             * We invalidate but we DON'T actually read the data - that would add
             * unnecessary loop overhead to our measurement.
             */
            Xil_DCacheInvalidateRange((INTPTR)&shared_mem[4], packet_size);
            
            /* 
             * Memory barrier to make sure all the invalidations actually finish
             * before we take our timestamp. Pretty important for accurate timing.
             */
            __asm__ __volatile__("dsb sy" ::: "memory");
            
            // Take the timestamp AFTER everything completes
            rpu_ts = read_timer();
            
            // Save this result
            store_result(packet_size, apu_ts, rpu_ts);
            
            packets_received++;
            
            // Send acknowledgment back to APU
            shared_mem[0] = MAGIC_ACK;
            flush_control_word();
            
            // Print progress every 100 packets
            if (packets_received % 100 == 0) {
                xil_printf("RPU: Received %u packets\r\n", packets_received);
            }
        }
        
        // Small delay between polls
        for (volatile int i = 0; i < 10; i++);
    }
    
    xil_printf("RPU: Total packets: %u\r\n", packets_received);
    
    // Store the final count and flush everything to memory
    results_mem[0] = result_count;
    flush_results();
}

/**
 * Main
 */
int main(void)
{
    xil_printf("\r\n========================================\r\n");
    xil_printf("RPU Cache Invalidation Overhead Measurement\r\n");
    xil_printf("========================================\r\n");
    xil_printf("Shared Memory: 0x%08X\r\n", SHARED_MEM_BASE);
    xil_printf("Results Area:  0x%08X\r\n", SHARED_MEM_BASE + RESULTS_OFFSET);
    xil_printf("TTC0 Base:     0x%08X\r\n", TTC0_BASE);
    xil_printf("\r\nNOTE: This version measures ONLY cache invalidation\r\n");
    xil_printf("overhead, NOT the time to read/process the actual data.\r\n");
    xil_printf("This represents the cost that CCI-400 would eliminate.\r\n");
    xil_printf("========================================\r\n\r\n");
    
    init_timer();
    
    // Clear out the results area before we start
    memset((void *)results_mem, 0, 4 + MAX_RESULTS * 20);
    result_count = 0;
    Xil_DCacheFlushRange((INTPTR)results_mem, 4 + MAX_RESULTS * 20);
    
    receiver_loop();
    
    xil_printf("\r\nRPU: Experiment complete.\r\n");
    
    // Just hang here when we're done
    while (1) {
        for (volatile int i = 0; i < 1000000; i++);
    }
    
    return 0;
}