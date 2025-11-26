/**
 * @file rpu_tcm_multisize.c
 * @brief RPU Firmware per test multi-size con TCM
 * 
 * Gestisce pacchetti di dimensioni variabili dalla TCM
 */

#include <stdio.h>
#include <stdint.h>
#include "xil_printf.h"
#include "sleep.h"

/* TCM Base Address per RPU */
#define TCM_BASE_ADDR           0x00000000

/* Shared Data Structure (matches APU) */
typedef struct {
    volatile uint32_t command;
    volatile uint32_t packet_size;
    volatile uint32_t apu_timestamp;
    volatile uint32_t rpu_timestamp;
    volatile uint32_t status;
    volatile uint32_t _pad[3];  // AGGIUNGI PADDING totale 32 bytes
    /* Data payload starts here */
    volatile uint8_t data[4096];  /* 4KB - 20 bytes header */
} __attribute__((packed, aligned(16))) TCM_Protocol;  // allinea a 16

/* Command codes */
#define CMD_IDLE        0x00000000
#define CMD_PROCESS     0x12345678
#define CMD_SHUTDOWN    0xDEADBEEF

/* Status codes */
#define STATUS_READY    0xAAAAAAAA
#define STATUS_BUSY     0xBBBBBBBB
#define STATUS_DONE     0xCCCCCCCC

/* Pointer to TCM protocol */
static volatile TCM_Protocol *tcm_proto = (volatile TCM_Protocol *)TCM_BASE_ADDR;

/**
 * Simple checksum for data verification
 */
uint32_t calculate_checksum(volatile uint8_t *data, uint32_t size)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size; i++) {
        sum += data[i];
    }
    return sum;
}

/**
 * Process packet - simple operation on data
 */
void process_packet(uint32_t size)
{
    /* Simple processing: calculate checksum */
    uint32_t checksum = calculate_checksum(tcm_proto->data, size);
    
    /* Could do more processing here */
    (void)checksum;  /* Avoid unused warning */
}

/**
 * Initialize
 */
void init_tcm_protocol(void)
{
    xil_printf("\r\n=== RPU TCM Multi-Size Reader ===\r\n");
    xil_printf("TCM Base: 0x%08X\r\n", TCM_BASE_ADDR);
    
    /* Initialize protocol */
    tcm_proto->command = CMD_IDLE;
    tcm_proto->packet_size = 0;
    tcm_proto->apu_timestamp = 0;
    tcm_proto->rpu_timestamp = 0;
    tcm_proto->status = STATUS_READY;
    
    xil_printf("RPU ready for multi-size packets\r\n");
}

/**
 * Main
 */
int main(void)
{
    uint32_t packets_processed = 0;
    
    init_tcm_protocol();
    
    xil_printf("\r\n=== Starting Main Loop ===\r\n");
    xil_printf("Waiting for packets from APU...\r\n\n");
    
    while (1) {
        uint32_t cmd = tcm_proto->command;
        
        switch (cmd) {
            case CMD_PROCESS:
                /* Mark as busy */
                tcm_proto->status = STATUS_BUSY;
                
                /* Get packet size and timestamp */
                uint32_t size = tcm_proto->packet_size;
                uint32_t apu_ts = tcm_proto->apu_timestamp;
                
                /* Record when we got it (could use actual timer here) */
                tcm_proto->rpu_timestamp = apu_ts + 10;  /* Placeholder */
                
                /* Process the packet */
                process_packet(size);
                
                /* Done */
                packets_processed++;
                tcm_proto->status = STATUS_DONE;
                tcm_proto->command = CMD_IDLE;
                
                /* Debug every 100 packets */
                if (packets_processed % 100 == 0) {
                    xil_printf("Processed %lu packets (last size: %lu bytes)\r\n", 
                               packets_processed, size);
                }
                break;
                
            case CMD_SHUTDOWN:
                xil_printf("\r\nShutdown received\r\n");
                xil_printf("Total packets processed: %lu\r\n", packets_processed);
                tcm_proto->status = STATUS_DONE;
                return 0;
                
            case CMD_IDLE:
            default:
                /* Idle */
                usleep(1);
                break;
        }
    }
    
    return 0;
}
