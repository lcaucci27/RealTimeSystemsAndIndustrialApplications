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
    volatile uint32_t rpu_timestamp;  /* When RPU received */
    volatile uint32_t status;
    volatile uint32_t _pad[3];  // padding to get 32 bytes total
    /* Data payload starts here */
    volatile uint8_t data[4096];  /* Rest of TCM for data */
} __attribute__((packed, aligned(16))) TCM_Protocol;  // 16-byte alignment

/* Command codes */
#define CMD_IDLE        0x00000000
#define CMD_PROCESS     0x12345678
#define CMD_SHUTDOWN    0xDEADBEEF

/* Status codes */
#define STATUS_READY    0xAAAAAAAA
#define STATUS_BUSY     0xBBBBBBBB
#define STATUS_DONE     0xCCCCCCCC

/* Global pointers */
static volatile TCM_Protocol *tcm_proto = NULL;
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
 * Wait for RPU done
 */
static int wait_for_done(int timeout_us)
{
    int elapsed = 0;
    
    while (elapsed < timeout_us) {
        if (tcm_proto->status == STATUS_DONE) {
            return 0;
        }
        //usleep(1);
        elapsed++;
    }
    
    return -1;
}

/**
 * Send one packet
 */
static int send_packet(uint32_t size, uint8_t *payload, uint32_t *delta_ticks)
{
    uint32_t ts_start, ts_end;
    
    /* Make sure size fits in TCM */
    if (size > 4096) {
        fprintf(stderr, "APU: Packet size %u too large for TCM\n", size);
        return -1;
    }
    
    /* Copy payload to TCM */
    if (payload && size > 0) {
        memcpy((void *)tcm_proto->data, payload, size);
    }
    
    /* Set packet size */
    tcm_proto->packet_size = size;
    
    /* Timestamp START, before final write operations */
    ts_start = read_timer();
    tcm_proto->apu_timestamp = ts_start;
    
    /* Memory barrier */
    __sync_synchronize();
    
    /* Signal RPU to process */
    tcm_proto->command = CMD_PROCESS;
    
    /* Timestamp END, right after write (not waiting for RPU) */
    ts_end = read_timer();
    
    /* Calculate write overhead only */
    if (ts_end >= ts_start) {
        *delta_ticks = ts_end - ts_start;
    } else {
        /* Handle timer overflow */
        *delta_ticks = (0xFFFF - ts_start) + ts_end;
    }
    
    /* Small delay to let RPU process before next packet */
    usleep(100);
    
    /* Reset for next iteration */
    tcm_proto->command = CMD_IDLE;
    
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
    
    /* Wait for RPU to be ready */
    if (wait_for_rpu_ready(30) != 0) {
        free(payload);
        return -1;
    }
    
    /* Open output file */
    fp = fopen(output_file, "w");
    if (!fp) {
        perror("Cannot open output file");
        free(payload);
        return -1;
    }
    
    /* CSV Header */
    fprintf(fp, "packet_size,apu_timestamp,rpu_timestamp,delta_ticks,delta_us\n");
    
    printf("APU: Starting test...\n\n");
    
    /* Test each size */
    for (size_idx = 0; size_idx < NUM_SIZES; size_idx++) {
        uint32_t pkt_size = packet_sizes[size_idx];
        
        printf("APU: Testing size %u bytes... ", pkt_size);
        fflush(stdout);
        
        for (iter = 0; iter < iterations_per_size; iter++) {
            uint32_t delta_ticks;
            
            if (send_packet(pkt_size, payload, &delta_ticks) == 0) {
                double delta_us = (double)delta_ticks / TIMER_FREQ_MHZ;
                
                fprintf(fp, "%u,%u,%u,%u,%.3f\n",
                        pkt_size,
                        tcm_proto->apu_timestamp,
                        tcm_proto->rpu_timestamp,
                        delta_ticks,
                        delta_us);
                
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