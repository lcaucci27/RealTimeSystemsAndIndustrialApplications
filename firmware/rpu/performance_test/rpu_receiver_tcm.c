#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <errno.h>

/* TCM Setup */
#define TCM_BASE            0xFFE00000UL
#define TCM_SIZE            0x10000UL     /* 64KB */

/* TTC0 Timer 0 Registers */
#define TTC0_BASE           0xFF110000UL
#define TTC0_SIZE           0x1000UL
#define TTC0_CLK_CTRL       0x00
#define TTC0_CNT_CTRL       0x0C
#define TTC0_CNT_VAL        0x18

/* Timer frequency */
#define TIMER_FREQ_MHZ      100.0

/* Results storage offset in TCM */
#define RESULTS_OFFSET      0x2000UL      /* Results at offset 8KB */
#define MAX_RESULTS         1000

/* Packet sizes to test (in bytes), limited by TCM size */
static const uint32_t packet_sizes[] = {
    1,      /* Minimum */
    4,      /* Word */
    16,     /* Small */
    32,     /* Small */
    64,     /* Cache line sized */
    128,    /* Typical cache line */
    256,    /* Medium */
    512,    /* Medium */
    1024,   /* 1 KB */
};
#define NUM_SIZES (sizeof(packet_sizes) / sizeof(packet_sizes[0]))

/* Shared Data Structure (matches RPU) */
typedef struct {
    volatile uint32_t command;
    volatile uint32_t packet_size;    /* Size of data to process */
    volatile uint32_t apu_timestamp;  /* When APU sent */
    volatile uint32_t rpu_timestamp;  /* When RPU received (filled by RPU) */
    volatile uint32_t status;
    volatile uint32_t _pad[3];
    /* Data payload starts here at offset 32 bytes */
    volatile uint8_t data[4096];
} __attribute__((packed, aligned(16))) TCM_Protocol;

/* Result structure, matches RPU output */
typedef struct {
    uint32_t packet_size;
    uint32_t apu_timestamp;
    uint32_t rpu_timestamp;
    uint32_t delta_ticks;
    uint32_t valid;
} __attribute__((packed)) result_entry_t;

/* Command codes */
#define CMD_IDLE        0x00000000
#define CMD_PROCESS     0x12345678
#define CMD_SHUTDOWN    0x87654321

/* Status codes */
#define STATUS_READY    0xAAAAAAAA
#define STATUS_BUSY     0xBBBBBBBB
#define STATUS_DONE     0xCCCCCCCC

/* Global pointers */
static volatile TCM_Protocol *tcm_proto = NULL;
static volatile uint32_t *results_mem = NULL;
static volatile uint32_t *timer_regs = NULL;
static int mem_fd = -1;

/**
 * Map physical memory
 */
static int map_memory(void)
{
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("Failed to open /dev/mem");
        return -1;
    }
    
    /* Map TCM */
    tcm_proto = (volatile TCM_Protocol *)mmap(
        NULL, TCM_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        mem_fd, TCM_BASE
    );
    if (tcm_proto == MAP_FAILED) {
        perror("Failed to map TCM");
        close(mem_fd);
        return -1;
    }
    
    /* Results area is inside TCM */
    results_mem = (volatile uint32_t *)((uint8_t *)tcm_proto + RESULTS_OFFSET);
    
    /* Map TTC0 timer */
    timer_regs = (volatile uint32_t *)mmap(
        NULL, TTC0_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        mem_fd, TTC0_BASE
    );
    if (timer_regs == MAP_FAILED) {
        perror("Failed to map TTC0 registers");
        munmap((void *)tcm_proto, TCM_SIZE);
        close(mem_fd);
        return -1;
    }
    
    printf("APU: TCM mapped at %p (phys 0x%08lX)\n", (void *)tcm_proto, TCM_BASE);
    printf("APU: Results area at %p (offset 0x%04lX)\n", (void *)results_mem, RESULTS_OFFSET);
    printf("APU: TTC0 mapped at %p (phys 0x%08lX)\n", (void *)timer_regs, TTC0_BASE);
    
    return 0;
}

/**
 * Clean up
 */
static void unmap_memory(void)
{
    if (timer_regs != MAP_FAILED && timer_regs != NULL) {
        munmap((void *)timer_regs, TTC0_SIZE);
    }
    if (tcm_proto != MAP_FAILED && tcm_proto != NULL) {
        munmap((void *)tcm_proto, TCM_SIZE);
    }
    if (mem_fd >= 0) {
        close(mem_fd);
    }
}

/**
 * Initialize TTC0 Timer
 */
static void init_timer(void)
{
    printf("APU: Initializing TTC0 Timer...\n");
    
    uint32_t cnt_ctrl = timer_regs[TTC0_CNT_CTRL / 4];
    printf("APU: TTC0 Counter Control = 0x%08X\n", cnt_ctrl);
    
    if (cnt_ctrl & 0x01) {
        printf("APU: Timer disabled, enabling...\n");
        timer_regs[TTC0_CNT_CTRL / 4] = 0x00;
    }
    
    uint32_t val1 = timer_regs[TTC0_CNT_VAL / 4];
    usleep(1000);
    uint32_t val2 = timer_regs[TTC0_CNT_VAL / 4];
    
    if (val2 != val1) {
        printf("APU: TTC0 running! Delta = %u ticks in 1ms\n", val2 - val1);
    } else {
        printf("APU: WARNING - TTC0 not incrementing!\n");
    }
}

/**
 * Read timer
 */
static inline uint32_t read_timer(void)
{
    return timer_regs[TTC0_CNT_VAL / 4];
}

/**
 * Wait for RPU ready
 */
static int wait_for_rpu_ready(int timeout_sec)
{
    time_t start = time(NULL);
    
    printf("APU: Waiting for RPU ready...\n");
    
    while (time(NULL) - start < timeout_sec) {
        if (tcm_proto->status == STATUS_READY) {
            printf("APU: RPU is ready!\n");
            return 0;
        }
        usleep(10000);
    }
    
    printf("APU: ERROR - RPU not ready after %d seconds\n", timeout_sec);
    printf("APU: Current status: 0x%08X\n", tcm_proto->status);
    return -1;
}

/**
 * Wait for RPU done (ACK)
 * wait for STATUS_DONE
 */
static int wait_for_done(int timeout_us)
{
    int elapsed = 0;
    
    while (elapsed < timeout_us) {
        if (tcm_proto->status == STATUS_DONE) {
            return 0;
        }
        usleep(1);
        elapsed++;
    }
    
    return -1;
}

/**
 * Send one packet
 * 
 * 1. Read timestamp
 * 2. Write payload
 * 3. Write metadata (size, timestamp)
 * 4. Signal RPU (command = CMD_PROCESS)
 * 5. Wait for ACK (status = STATUS_DONE)
 * 
 * RPU calculates delta and saves it in results
 */
static int send_packet(uint32_t size, uint8_t *payload)
{
    uint32_t ts_start;
    
    /* Make sure size fits in TCM */
    if (size > 4096) {
        fprintf(stderr, "APU: Packet size %u too large for TCM\n", size);
        return -1;
    }
    
    /* Copy payload if we have one */
    if (payload && size > 0) {
        memcpy((void *)tcm_proto->data, payload, size);
    }
    
    /* Set packet size */
    tcm_proto->packet_size = size;
    
    /* Timestamp START, BEFORE signal */
    ts_start = read_timer();
    tcm_proto->apu_timestamp = ts_start;
    
    /* Memory barrier */
    __sync_synchronize();
    
    /* Signal RPU*/
    tcm_proto->command = CMD_PROCESS;
    
    /* Wait for ACK*/
    if (wait_for_done(10000) != 0) {
        fprintf(stderr, "APU: Timeout waiting for RPU ACK\n");
        return -1;
    }
    
    /* Reset for next packet */
    tcm_proto->command = CMD_IDLE;
    
    return 0;
}

/**
 * Read results from RPU
 */
static int read_results(FILE *fp)
{
    uint32_t count = results_mem[0];
    
    printf("APU: Reading %u results from RPU...\n", count);
    
    if (count == 0 || count > MAX_RESULTS) {
        fprintf(stderr, "APU: Invalid result count: %u\n", count);
        return -1;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t offset = 1 + (i * 5);
        
        uint32_t pkt_size = results_mem[offset + 0];
        uint32_t apu_ts = results_mem[offset + 1];
        uint32_t rpu_ts = results_mem[offset + 2];
        uint32_t delta_ticks = results_mem[offset + 3];
        uint32_t valid = results_mem[offset + 4];
        
        if (valid != 0xA5A5A5A5) {
            fprintf(stderr, "APU: Invalid result marker at index %u\n", i);
            continue;
        }
        
        double delta_us = (double)delta_ticks / TIMER_FREQ_MHZ;
        
        fprintf(fp, "%u,%u,%u,%u,%.3f\n",
                pkt_size, apu_ts, rpu_ts, delta_ticks, delta_us);
    }
    
    printf("APU: Successfully read %u results\n", count);
    
    return 0;
}

/**
 * Run experiment
 */
static int run_experiment(int iterations_per_size, const char *output_file)
{
    FILE *fp;
    uint8_t *payload;
    int size_idx, iter;
    int total_packets = 0;
    int failed_packets = 0;
    
    printf("\n========================================\n");
    printf("APU TCM Multi-Size Performance Test\n");
    printf("========================================\n");
    printf("Iterations per size: %d\n", iterations_per_size);
    printf("Number of sizes: %ld\n", NUM_SIZES);
    printf("Total packets: %ld\n", NUM_SIZES * iterations_per_size);
    printf("Output file: %s\n", output_file);
    printf("========================================\n\n");
    
    /* Allocate buffer for largest packet */
    payload = (uint8_t *)malloc(packet_sizes[NUM_SIZES - 1]);
    if (!payload) {
        perror("Failed to allocate payload");
        return -1;
    }
    
    /* Fill with test pattern */
    for (uint32_t i = 0; i < packet_sizes[NUM_SIZES - 1]; i++) {
        payload[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Clear results area */
    printf("APU: Clearing results area...\n");
    memset((void *)results_mem, 0, 4 + MAX_RESULTS * 20);
    
    /* Wait for RPU to be ready */
    if (wait_for_rpu_ready(30) != 0) {
        free(payload);
        return -1;
    }
    
    printf("APU: Starting test...\n\n");
    
    /* Test each size */
    for (size_idx = 0; size_idx < NUM_SIZES; size_idx++) {
        uint32_t pkt_size = packet_sizes[size_idx];
        
        printf("APU: Testing size %u bytes... ", pkt_size);
        fflush(stdout);
        
        for (iter = 0; iter < iterations_per_size; iter++) {
            if (send_packet(pkt_size, payload) == 0) {
                total_packets++;
            } else {
                failed_packets++;
            }
            
            usleep(100);  /* Small delay between packets */
        }
        
        printf("Done (%d/%d)\n", iterations_per_size - failed_packets, iterations_per_size);
    }
    
    printf("\nAPU: Sending shutdown...\n");
    tcm_proto->command = CMD_SHUTDOWN;
    usleep(100000);
    
    /* Open output file */
    fp = fopen(output_file, "w");
    if (!fp) {
        perror("Cannot open output file");
        free(payload);
        return -1;
    }
    
    /* CSV Header */
    fprintf(fp, "packet_size,apu_timestamp,rpu_timestamp,delta_ticks,delta_us\n");
    
    /* Read results from RPU */
    if (read_results(fp) != 0) {
        fprintf(stderr, "APU: Failed to read results\n");
    }
    
    printf("\n========================================\n");
    printf("Test Complete\n");
    printf("========================================\n");
    printf("Total packets sent: %d\n", total_packets);
    printf("Failed packets: %d\n", failed_packets);
    printf("Success rate: %.1f%%\n", 100.0 * total_packets / (total_packets + failed_packets));
    printf("========================================\n");
    
    fclose(fp);
    free(payload);
    
    return 0;
}

/**
 * Main
 */
int main(int argc, char *argv[])
{
    int iterations_per_size = 100;
    const char *output_file = "tcm_multisize_results.csv";
    
    if (argc > 1) {
        iterations_per_size = atoi(argv[1]);
    }
    if (argc > 2) {
        output_file = argv[2];
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║  APU-RPU TCM Multi-Size Performance Test ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    printf("\n");
    
    if (map_memory() < 0) {
        return EXIT_FAILURE;
    }
    
    init_timer();
    
    if (run_experiment(iterations_per_size, output_file) < 0) {
        unmap_memory();
        return EXIT_FAILURE;
    }
    
    unmap_memory();
    
    printf("\nTest completed successfully!\n");
    printf("Results saved to: %s\n\n", output_file);
    
    return EXIT_SUCCESS;
}