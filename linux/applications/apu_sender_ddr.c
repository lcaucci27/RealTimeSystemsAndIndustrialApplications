/*
 * APU Sender - Performance Measurement Orchestrator (TTC Version)
 * 
 * Linux userspace app that runs on the APU (Cortex-A53) and sends
 * data packets to RPU through shared memory while measuring transfer times.
 * 
 * Compile: aarch64-linux-gnu-gcc -O2 -o apu_sender_ttc apu_sender_ttc.c -lrt
 * Run: sudo ./apu_sender_ttc [num_iterations] [output_file]
 * 
 * Timer: TTC0 Timer 0 at 0xFF110000 (~100 MHz)
 * Shared Memory: 0x3E000000 (configured in device tree)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <errno.h>

/* Shared Memory Setup */
#define SHARED_MEM_BASE     0x3E000000UL
#define SHARED_MEM_SIZE     0x00800000UL  /* 8 MB */

/* Protocol Magic Values */
#define MAGIC_START         0x0F0F0F0FUL
#define MAGIC_ACK           0xF0F0F0F0UL
#define MAGIC_DONE          0xFFFFFFFFUL
#define MAGIC_READY         0xAAAAAAAAUL

/* TTC0 Timer 0 Registers */
#define TTC0_BASE           0xFF110000UL
#define TTC0_SIZE           0x1000UL
#define TTC0_CLK_CTRL       0x00  /* Clock Control */
#define TTC0_CNT_CTRL       0x0C  /* Counter Control */
#define TTC0_CNT_VAL        0x18  /* Counter Value */

/* Timer frequency */
#define TIMER_FREQ_HZ       100000000UL  /* ~100 MHz */
#define TIMER_FREQ_MHZ      100.0

/* Results storage offset */
#define RESULTS_OFFSET      0x00400000UL  /* 4 MB offset */

/* Packet sizes to test (in bytes) */
static const uint32_t packet_sizes[] = {
    1,      /* Minimum */
    16,     /* Small */
    32,     /* Small */
    64,     /* Cache line sized */
    128,    /* Typical cache line */
    256,    /* Medium */
    512,    /* Medium */
    1024,   /* 1 KB */
    2048,   /* 2 KB */
    4096,   /* 4 KB - page size */
    8192,   /* 8 KB */
    16384,  /* 16 KB */
    32768,  /* 32 KB */
    65536   /* 64 KB */
};
#define NUM_SIZES (sizeof(packet_sizes) / sizeof(packet_sizes[0]))

/* Global pointers */
static volatile uint32_t *shared_mem = NULL;
static volatile uint32_t *timer_regs = NULL;
static volatile uint32_t *results_mem = NULL;
static int mem_fd = -1;

/* Result structure (must match RPU side) */
typedef struct {
    uint32_t packet_size;
    uint32_t apu_timestamp;
    uint32_t rpu_timestamp;
    uint32_t delta_ticks;
    uint32_t valid;
} __attribute__((packed)) result_entry_t;

/**
 * Map physical memory using /dev/mem
 */
static int map_memory(void)
{
    // Need /dev/mem for direct physical memory access
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("Failed to open /dev/mem");
        return -1;
    }
    
    // Map shared memory region
    shared_mem = (volatile uint32_t *)mmap(
        NULL, SHARED_MEM_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        mem_fd, SHARED_MEM_BASE
    );
    if (shared_mem == MAP_FAILED) {
        perror("Failed to map shared memory");
        close(mem_fd);
        return -1;
    }
    
    // Map TTC0 timer registers
    timer_regs = (volatile uint32_t *)mmap(
        NULL, TTC0_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        mem_fd, TTC0_BASE
    );
    if (timer_regs == MAP_FAILED) {
        perror("Failed to map TTC0 registers");
        munmap((void *)shared_mem, SHARED_MEM_SIZE);
        close(mem_fd);
        return -1;
    }
    
    // Results area is just an offset in shared memory
    results_mem = (volatile uint32_t *)((uint8_t *)shared_mem + RESULTS_OFFSET);
    
    printf("APU: Memory mapped successfully\n");
    printf("APU: Shared memory at %p (phys 0x%08lX)\n", 
           (void *)shared_mem, SHARED_MEM_BASE);
    printf("APU: TTC0 registers at %p (phys 0x%08lX)\n", 
           (void *)timer_regs, TTC0_BASE);
    printf("APU: Results area at %p\n", (void *)results_mem);
    
    return 0;
}

/**
 * Clean up memory mappings
 */
static void unmap_memory(void)
{
    if (timer_regs != MAP_FAILED && timer_regs != NULL) {
        munmap((void *)timer_regs, TTC0_SIZE);
    }
    if (shared_mem != MAP_FAILED && shared_mem != NULL) {
        munmap((void *)shared_mem, SHARED_MEM_SIZE);
    }
    if (mem_fd >= 0) {
        close(mem_fd);
    }
}

/**
 * Initialize TTC0 Timer 0
 */
static void init_timer(void)
{
    printf("APU: Initializing TTC0 Timer 0...\n");
    
    // Check current state
    uint32_t cnt_ctrl = timer_regs[TTC0_CNT_CTRL / 4];
    printf("APU: TTC0 Counter Control = 0x%08X\n", cnt_ctrl);
    
    // Enable timer if it's disabled (bit 0 = 1 means disabled)
    if (cnt_ctrl & 0x01) {
        printf("APU: Timer is disabled, enabling...\n");
        timer_regs[TTC0_CNT_CTRL / 4] = 0x00;  /* Enable, overflow mode, increment */
    }
    
    // Quick check to see if it's actually running
    uint32_t val1 = timer_regs[TTC0_CNT_VAL / 4];
    usleep(1000);  /* Wait 1ms */
    uint32_t val2 = timer_regs[TTC0_CNT_VAL / 4];
    
    if (val2 != val1) {
        printf("APU: TTC0 Timer running! val1=0x%08X, val2=0x%08X\n", val1, val2);
        printf("APU: Delta = %u ticks in 1ms (expected ~100,000)\n", val2 - val1);
    } else {
        printf("APU: WARNING - TTC0 Timer not incrementing!\n");
    }
}

/**
 * Read timer counter value
 */
static inline uint32_t read_timer(void)
{
    return timer_regs[TTC0_CNT_VAL / 4];
}

/**
 * Wait for RPU to signal ready
 */
static int wait_for_rpu_ready(int timeout_sec)
{
    time_t start = time(NULL);
    
    printf("APU: Waiting for RPU to be ready...\n");
    
    while (time(NULL) - start < timeout_sec) {
        if (shared_mem[0] == MAGIC_READY) {
            printf("APU: RPU is ready!\n");
            return 0;
        }
        usleep(10000);  /* Check every 10ms */
    }
    
    printf("APU: ERROR - RPU not ready after %d seconds\n", timeout_sec);
    return -1;
}

/**
 * Wait for RPU acknowledgment
 */
static int wait_for_ack(int timeout_us)
{
    int elapsed = 0;
    
    while (elapsed < timeout_us) {
        if (shared_mem[0] == MAGIC_ACK) {
            return 0;
        }
        usleep(1);
        elapsed++;
    }
    
    return -1;
}

/**
 * Send one packet to RPU
 */
static int send_packet(uint32_t size, uint8_t *payload)
{
    uint32_t ts;
    
    // Copy payload into shared memory if we have any
    if (payload && size > 0) {
        memcpy((void *)&shared_mem[4], payload, size);
    }
    
    // Write metadata (size in word 1)
    shared_mem[1] = size;
    
    // Grab timestamp right before signaling RPU
    ts = read_timer();
    shared_mem[2] = ts;
    shared_mem[3] = 0;  /* Reserved */
    
    // Memory barrier to ensure everything's written
    __sync_synchronize();
    
    // Signal packet is ready
    shared_mem[0] = MAGIC_START;
    
    // Wait for RPU to acknowledge (10ms timeout)
    if (wait_for_ack(10000) != 0) {
        fprintf(stderr, "APU: WARNING - No ACK for packet size %u\n", size);
        return -1;
    }
    
    return 0;
}

/**
 * Run the experiment
 */
static int run_experiment(int iterations_per_size, const char *output_file)
{
    FILE *fp;
    uint8_t *payload;
    int size_idx, iter;
    int total_packets = 0;
    int failed_packets = 0;
    
    printf("\n========================================\n");
    printf("APU Performance Measurement Sender\n");
    printf("(TTC0 Timer Version)\n");
    printf("========================================\n");
    printf("Iterations per size: %d\n", iterations_per_size);
    printf("Number of packet sizes: %zu\n", NUM_SIZES);
    printf("Total packets to send: %zu\n", NUM_SIZES * iterations_per_size);
    printf("Output file: %s\n", output_file);
    printf("========================================\n\n");
    
    // Allocate buffer for largest packet
    payload = (uint8_t *)malloc(packet_sizes[NUM_SIZES - 1]);
    if (!payload) {
        perror("Failed to allocate payload buffer");
        return -1;
    }
    
    // Fill with simple test pattern
    for (uint32_t i = 0; i < packet_sizes[NUM_SIZES - 1]; i++) {
        payload[i] = (uint8_t)(i & 0xFF);
    }
    
    // Make sure RPU is ready before we start
    if (wait_for_rpu_ready(30) != 0) {
        free(payload);
        return -1;
    }
    
    printf("APU: Starting experiment...\n\n");
    
    // Test each packet size
    for (size_idx = 0; size_idx < NUM_SIZES; size_idx++) {
        uint32_t pkt_size = packet_sizes[size_idx];
        
        printf("APU: Testing packet size: %u bytes\n", pkt_size);
        
        // Send multiple iterations for stats
        for (iter = 0; iter < iterations_per_size; iter++) {
            if (send_packet(pkt_size, payload) == 0) {
                total_packets++;
            } else {
                failed_packets++;
            }
            
            // Small delay between packets
            usleep(100);  /* 100us */
        }
        
        printf("APU: Completed %d iterations for size %u\n", 
               iterations_per_size, pkt_size);
    }
    
    printf("\nAPU: Sending DONE signal...\n");
    shared_mem[0] = MAGIC_DONE;