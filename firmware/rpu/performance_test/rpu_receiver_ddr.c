/*
 * RPU Receiver - Cache Invalidation Overhead Measurement
 * 
 * This version measures ONLY the cache invalidation overhead.
 * We're trying to figure out what cost CCI-400 hardware coherence would save us.
 * 
 * Timer: TTC0 Timer 0 at 0xFF110000 (~100 MHz)
 * Shared Memory: 0x3E000000
 */

#include <stdint.h>
#include <string.h>
#include "xil_printf.h"
#include "xil_cache.h"
#include "xil_io.h"

/* Shared Memory Setup */
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
    
    // Stop the counter first
    Xil_Out32(TTC0_CNT_CTRL, 0x01);
    
    // Configure clock with no prescaler
    Xil_Out32(TTC0_CLK_CTRL, 0x00);
    
    // Start it
    Xil_Out32(TTC0_CNT_CTRL, 0x00);
    
    // Quick check to make sure it's actually ticking
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
 * Read timer value
 */
static inline uint32_t read_timer(void)
{
    return Xil_In32(TTC0_CNT_VAL);
}

/**
 * Invalidate just the control word (first cache line)
 */
static inline void invalidate_control_word(void)
{
    Xil_DCacheInvalidateRange((INTPTR)shared_mem, CACHE_LINE_SIZE);
}

/**
 * Flush just the control word
 */
static inline void flush_control_word(void)
{
    Xil_DCacheFlushRange((INTPTR)shared_mem, CACHE_LINE_SIZE);
}

/**
 * Flush results area to memory
 */
static inline void flush_results(void)
{
    uint32_t bytes_to_flush = 4 + (result_count * 20);
    Xil_DCacheFlushRange((INTPTR)results_mem, bytes_to_flush);
}

/**
 * Store a result entry
 */
static void store_result(uint32_t pkt_size, uint32_t apu_ts, uint32_t rpu_ts)
{
    if (result_count >= MAX_RESULTS) return;
    
    // Calculate delta, handling wraparound
    uint32_t delta;
    if (rpu_ts >= apu_ts) {
        delta = rpu_ts - apu_ts;
    } else {
        delta = (0xFFFFFFFF - apu_ts) + rpu_ts + 1;
    }
    
    // Pack everything into the results buffer
    uint32_t offset = 1 + (result_count * 5);
    results_mem[offset + 0] = pkt_size;
    results_mem[offset + 1] = apu_ts;
    results_mem[offset + 2] = rpu_ts;
    results_mem[offset + 3] = delta;
    results_mem[offset + 4] = 0xA5A5A5A5UL;  // Validation marker
    
    result_count++;
}

/**
 * Main receiver loop - measures ONLY cache invalidation overhead
 * 
 * What we're measuring:
 * 1. Time for APU to write data to DRAM (uncached on APU side with O_SYNC)
 * 2. Time for RPU to poll and detect the signal
 * 3. Time for RPU to invalidate cache for metadata
 * 4. Time for RPU to invalidate cache for payload
 * 
 * What we're NOT measuring: time to actually read and process the data.
 * That cost would be there regardless of cache coherence.
 * 
 * The cache invalidation operations are what CCI-400 would eliminate entirely.
 */
static void receiver_loop(void)
{
    uint32_t rpu_ts, apu_ts, packet_size;
    uint32_t packets_received = 0;
    
    xil_printf("RPU: Entering receiver loop (INVALIDATION OVERHEAD ONLY)...\r\n");
    xil_printf("RPU: Waiting for packets at 0x%08X\r\n", (uint32_t)shared_mem);
    
    // Signal APU that we're ready
    shared_mem[0] = MAGIC_READY;
    flush_control_word();
    
    while (1) {
        // Only invalidate the control word for polling
        invalidate_control_word();
        
        // Check if we're done
        if (shared_mem[0] == MAGIC_DONE) {
            xil_printf("RPU: Received DONE signal\r\n");
            break;
        }
        
        // Check for new packet
        if (shared_mem[0] == MAGIC_START) {
            /* 
             * Here's the real overhead that CCI-400 would eliminate:
             * - Invalidate metadata cache lines
             * - Read metadata (size and timestamp)
             * - Invalidate payload cache lines
             * 
             * We intentionally DON'T read through the payload byte-by-byte.
             * Reading the actual data would take time even with hardware coherence,
             * so we're only measuring the pure cache management overhead.
             */
            
            // Invalidate metadata (first 256 bytes = 4 cache lines)
            Xil_DCacheInvalidateRange((INTPTR)shared_mem, 256);
            
            // Read what we need from metadata
            packet_size = shared_mem[1];
            apu_ts = shared_mem[2];
            
            /* 
             * This is the key part: invalidate the payload cache lines.
             * This overhead is exactly what CCI-400 would get rid of!
             * We invalidate but don't read the data - that would add
             * unnecessary overhead to our measurement.
             */
            Xil_DCacheInvalidateRange((INTPTR)&shared_mem[4], packet_size);
            
            /* 
             * Memory barrier to ensure all invalidations complete before timestamp.
             * Pretty important for accurate timing.
             */
            __asm__ __volatile__("dsb sy" ::: "memory");
            
            // Timestamp AFTER everything's done
            rpu_ts = read_timer();
            
            // Save this measurement
            store_result(packet_size, apu_ts, rpu_ts);
            
            packets_received++;
            
            // Acknowledge back to APU
            shared_mem[0] = MAGIC_ACK;
            flush_control_word();
            
            // Progress update every 100 packets
            if (packets_received % 100 == 0) {
                xil_printf("RPU: Received %u packets\r\n", packets_received);
            }
        }
        
        // Small delay between polls
        for (volatile int i = 0; i < 10; i++);
    }
    
    xil_printf("RPU: Total packets: %u\r\n", packets_received);
    
    // Write final count and flush everything
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
    
    // Clear results area
    memset((void *)results_mem, 0, 4 + MAX_RESULTS * 20);
    result_count = 0;
    Xil_DCacheFlushRange((INTPTR)results_mem, 4 + MAX_RESULTS * 20);
    
    receiver_loop();
    
    xil_printf("\r\nRPU: Experiment complete.\r\n");
    
    // Hang here when done
    while (1) {
        for (volatile int i = 0; i < 1000000; i++);
    }
    
    return 0;
}