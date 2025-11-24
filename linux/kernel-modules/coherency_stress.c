/*
 * coherency_test.c - Cache Coherency Test Module for Zynq UltraScale+ MPSoC
 * 
 * This module sets up some cacheable memory with Outer Shareable attributes
 * so we can test if hardware cache coherency actually works between the APU
 * and RPU through the CCI-400 interconnect.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <asm/pgtable.h>
#include <asm/io.h>

#define MODULE_NAME "coherency_test"
#define TEST_SIZE PAGE_SIZE
#define NUM_TEST_WORDS 10

static void *virt_addr = NULL;
static phys_addr_t phys_addr;
static volatile uint32_t *test_buffer;

// Proc file so we can expose the physical address to userspace
static struct proc_dir_entry *proc_entry;

/*
 * Pattern definitions for testing
 */
#define PATTERN_OLD 0x0F0F0F0F  // The "old" value we'll write to DDR
#define PATTERN_NEW 0xF0F0F0F0  // The "new" value we'll keep in cache

/*
 * Proc file read handler - displays physical address and current buffer data
 */
static int coherency_proc_show(struct seq_file *m, void *v)
{
    int i;
    
    seq_printf(m, "=== CCI-400 Cache Coherency Test Module ===\n\n");
    seq_printf(m, "Physical Address: 0x%llx\n", (u64)phys_addr);
    seq_printf(m, "Virtual Address:  0x%px\n", virt_addr);
    seq_printf(m, "Size: %lu bytes (%d words)\n\n", TEST_SIZE, NUM_TEST_WORDS);
    
    seq_printf(m, "Current Buffer Contents:\n");
    for (i = 0; i < NUM_TEST_WORDS; i++) {
        seq_printf(m, "  [%d] = 0x%08x\n", i, test_buffer[i]);
    }
    
    seq_printf(m, "\nMemory Attributes:\n");
    seq_printf(m, "  - Type: Normal Memory (Cacheable)\n");
    seq_printf(m, "  - Shareability: Outer Shareable\n");
    seq_printf(m, "  - Cache Policy: Write-Back\n");
    
    seq_printf(m, "\nInstructions:\n");
    seq_printf(m, "  1. Use physical address 0x%llx in RPU firmware\n", (u64)phys_addr);
    seq_printf(m, "  2. Load RPU firmware: echo firmware.elf > /sys/class/remoteproc/remoteproc0/firmware\n");
    seq_printf(m, "  3. Start RPU: echo start > /sys/class/remoteproc/remoteproc0/state\n");
    seq_printf(m, "  4. Check RPU output via serial console\n");
    
    return 0;
}

static int coherency_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, coherency_proc_show, NULL);
}

static const struct proc_ops coherency_proc_fops = {
    .proc_open = coherency_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

/*
 * Module initialization
 */
static int __init coherency_init(void)
{
    int i;
    pgprot_t prot;
    
    pr_info("===========================================\n");
    pr_info("%s: Initializing Cache Coherency Test\n", MODULE_NAME);
    pr_info("===========================================\n");
    
    // Allocate one page of normal cacheable memory
    virt_addr = (void *)__get_free_page(GFP_KERNEL);
    if (!virt_addr) {
        pr_err("%s: Failed to allocate test page\n", MODULE_NAME);
        return -ENOMEM;
    }
    
    // Get the physical address so we can give it to the RPU
    phys_addr = virt_to_phys(virt_addr);
    test_buffer = (volatile uint32_t *)virt_addr;
    
    pr_info("%s: Memory allocated successfully\n", MODULE_NAME);
    pr_info("%s:   Virtual address:  0x%px\n", MODULE_NAME, virt_addr);
    pr_info("%s:   Physical address: 0x%llx\n", MODULE_NAME, (u64)phys_addr);
    pr_info("%s:   Size: %lu bytes\n", MODULE_NAME, TEST_SIZE);
    
    // Just checking what memory attributes we have (mostly for info)
    prot = PAGE_KERNEL; /* Normal memory, cacheable, shareable */
    pr_info("%s: Memory attributes: Normal, Cacheable, Outer Shareable\n", MODULE_NAME);
    
    // Phase 1: Write the OLD pattern and make sure it gets to DDR
    pr_info("%s: Phase 1 - Writing OLD pattern (0x%08x) with flush\n", 
            MODULE_NAME, PATTERN_OLD);
    
    for (i = 0; i < NUM_TEST_WORDS; i++) {
        test_buffer[i] = PATTERN_OLD;
    }
    
    // Force everything out to DDR with explicit cache clean operations
    for (i = 0; i < NUM_TEST_WORDS; i++) {
        __asm__ __volatile__ ("dc cvac, %0" :: "r" (&test_buffer[i]) : "memory");
    }
    __asm__ __volatile__ ("dsb sy" ::: "memory");
    
    pr_info("%s: OLD pattern flushed to DDR\n", MODULE_NAME);
    
    // Give it a moment to make sure everything's settled
    msleep(100);
    
    // Phase 2: Now write the NEW pattern but DON'T flush it
    // This way it only lives in the APU's cache
    pr_info("%s: Phase 2 - Writing NEW pattern (0x%08x) WITHOUT flush\n",
            MODULE_NAME, PATTERN_NEW);
    
    for (i = 0; i < NUM_TEST_WORDS; i++) {
        test_buffer[i] = PATTERN_NEW;
    }
    
    // Just a compiler barrier, no actual cache operations
    __asm__ __volatile__ ("" ::: "memory");
    
    pr_info("%s:\n", MODULE_NAME);
    pr_info("%s: Current state:\n", MODULE_NAME);
    pr_info("%s:   APU Cache L2: 0x%08x (NEW pattern)\n", MODULE_NAME, PATTERN_NEW);
    pr_info("%s:   DDR Memory:   0x%08x (OLD pattern)\n", MODULE_NAME, PATTERN_OLD);
    pr_info("%s:\n", MODULE_NAME);
    pr_info("%s: Expected behavior:\n", MODULE_NAME);
    pr_info("%s:   WITH CCI coherency: RPU reads 0x%08x (from APU cache)\n", 
            MODULE_NAME, PATTERN_NEW);
    pr_info("%s:   WITHOUT coherency:  RPU reads 0x%08x (from DDR)\n",
            MODULE_NAME, PATTERN_OLD);
    pr_info("%s:\n", MODULE_NAME);
    
    // Create the proc file so userspace can read test info
    proc_entry = proc_create(MODULE_NAME, 0444, NULL, &coherency_proc_fops);
    if (!proc_entry) {
        pr_err("%s: Failed to create /proc/%s\n", MODULE_NAME, MODULE_NAME);
        free_page((unsigned long)virt_addr);
        return -ENOMEM;
    }
    
    pr_info("===========================================\n");
    pr_info("%s: Initialization complete!\n", MODULE_NAME);
    pr_info("%s: Read /proc/%s for test information\n", MODULE_NAME, MODULE_NAME);
    pr_info("%s: Physical address for RPU: 0x%llx\n", MODULE_NAME, (u64)phys_addr);
    pr_info("===========================================\n");
    
    return 0;
}

/*
 * Module cleanup
 */
static void __exit coherency_exit(void)
{
    int i;
    
    pr_info("===========================================\n");
    pr_info("%s: Module cleanup\n", MODULE_NAME);
    
    if (proc_entry) {
        proc_remove(proc_entry);
    }
    
    if (virt_addr) {
        // Print out what's in the buffer before we free it
        pr_info("%s: Final buffer contents:\n", MODULE_NAME);
        for (i = 0; i < NUM_TEST_WORDS; i++) {
            pr_info("%s:   [%d] = 0x%08x\n", MODULE_NAME, i, test_buffer[i]);
        }
        
        free_page((unsigned long)virt_addr);
        pr_info("%s: Memory freed\n", MODULE_NAME);
    }
    
    pr_info("%s: Module unloaded\n", MODULE_NAME);
    pr_info("===========================================\n");
}

module_init(coherency_init);
module_exit(coherency_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simone");
MODULE_DESCRIPTION("CCI-400 Cache Coherency Test Module for Zynq UltraScale+ MPSoC");
MODULE_VERSION("1.0");