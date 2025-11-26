# Zynq UltraScale+ MPSoC Cache Coherence Analysis

Experimental analysis of cache coherence mechanisms in Xilinx Zynq UltraScale+ MPSoC, focusing on CCI-400 hardware coherence between APU (Cortex-A53) and RPU (Cortex-R5F) cores.

## 👥 Authors

- **Luigi Caucci** (M63001679)
- **Simone Cecere** (M63001681)

**Course:** Real Time Systems And Industrial Applications  
**Institution:** Università degli Studi di Napoli Federico II  
**Platform:** Xilinx Kria KR260
**Academic Year:** 2024/2025

---

## 📖 Project Overview

This project investigates cache coherence in heterogeneous multiprocessor systems, specifically analyzing the ARM CCI-400 (Cache Coherent Interconnect) in Zynq UltraScale+ MPSoC architecture. Through experimental measurements and theoretical modeling, we quantify the overhead of manual cache management and estimate potential performance gains with functional hardware coherence.

### Key Findings

- **Cache Invalidation Overhead:** ~1.5-3.5 µs for typical packet sizes (baseline non-coherent scenario)
- **CCI-400 Status:** Not operational in standard Xilinx BSP due to missing PMUFW initialization
- **Theoretical Speedup:** 30-50% improvement for small packets (<128B) with functional hardware coherence
- **Measurement Method:** Direct timer-based profiling with TTC0 @ 100MHz (10ns resolution)

---

## 🚀 Quick Start

### Prerequisites

**Hardware:**
- Xilinx Kria KR260 Starter Kit (or equivalent Zynq UltraScale+ board)
- SD card with PetaLinux 2022.2
- USB-UART cable for debug console
- Network connection (Optional: for SSH access)

**Software:**
- Vitis 2022.2 (for RPU firmware compilation)
- PetaLinux 2022.2 SDK (for cross-compilation)
- Python 3.8+ with numpy, pandas, matplotlib
- Linux development machine (Ubuntu 20.04+ recommended)

### 1. Build RPU Firmware

```bash
# Using Vitis IDE (recommended)
# 1. Open Vitis 2022.2
# 2. Create new application project
# 3. Platform: zynqmp_fsbl_bsp
# 4. Domain: standalone on psu_cortexr5_0
# 5. Import firmware/rpu/performance_test/rpu_perf_test.c
# 6. Build project → generates .elf file

# Or use script (requires Vitis CLI)
cd scripts
./build_rpu.sh
```

### 2. Build APU Application

```bash
cd linux/applications

# Cross-compile for ARM64
make

# Or manually:
aarch64-linux-gnu-gcc -O2 -o <OUTPUT> <SOURCE.c> -lrt
```

### 3.1. Deploy to Board (PuTTY)

Connect your board through USB-JTAG

Serial line: COMX
Speed (baud): 115200
Data bits: 8
Stop bits: 1
Parity: None
Flow control: None

### 3.2. Deploy to Board (SSH)

```bash
# Set your board IP
export BOARD_IP=192.168.1.100
```

### 4. Run Performance Tests

```bash
# On the board (via PuTTY)
# Load RPU firmware via Remoteproc
echo <FIRMWARE.elf> > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Wait a couple seconds for RPU initialization
sleep 2

# Run APU test (generates results.csv)
./apu_perf_test <REPETITIONS> <OUTPUT.csv>

# Results saved to csv
```

### 5. Analyze Results

```bash
# Copy results from board to USB drive
scp /home/root mnt/usb

# Run analysis script
python3 <ANALYSIS_SCRIPT.py> <OUTPUT.csv> output/

# Generated files:
# - output_comparison.png       : Main comparison plot
# - output_report.txt           : Detailed analysis report
# - output_stats.csv            : Statistical summary
```

---

## 📊 Experimental Methodology

### Test Scenarios

We implemented multiple test approaches to isolate different aspects of cache coherence:

#### 1. **Performance Measurement Test** (Primary)
- **Location:** `firmware/rpu/performance_test/` + `linux/applications/apu_sender_ddr.c`
- **Purpose:** Measure cache invalidation overhead in non-coherent scenario
- **Method:** 
  - APU writes data to shared memory (uncached via O_SYNC)
  - APU signals RPU via magic value
  - RPU invalidates cache lines for metadata and payload
  - RPU takes timestamp immediately after invalidation (before reading data)
  - Delta = Time to invalidate cache (overhead that CCI-400 would eliminate)
- **Packet Sizes:** 1B, 16B, 32B, 64B, 128B, 256B, 512B, 1KB, 2KB, 4KB, 8KB, 16KB, 32KB, 64KB
- **Iterations:** 100 per size (1400 total measurements)

#### 2. **Basic Coherence Test** (Verification)
- **Location:** `firmware/rpu/coherence_test/` + `linux/applications/apu_coherency_test.c`
- **Purpose:** Verify basic APU-RPU communication
- **Method:** Simple ping-pong test with cache enabled (no explicit management)
- **Expected Result:** Fails without CCI-400 (demonstrates need for manual cache ops)

#### 3. **Kernel Module Stress Test** (Advanced)
- **Location:** `linux/kernel-modules/coherency_stress.c`
- **Purpose:** High-frequency write test from kernel space
- **Method:**
  - Kernel module writes alternating patterns (OLD/NEW) without cache flush
  - RPU reads continuously and counts pattern occurrences
  - If NEW pattern visible → coherence working
  - If only OLD pattern → no coherence (RPU sees stale DRAM)
- **Expected Result:** Confirms CCI-400 not operational (0% NEW pattern detection)

### Measurement Details

**Timer Configuration:**
- **Timer:** TTC0 (Triple Timer Counter 0) at 0xFF110000
- **Frequency:** 100 MHz
- **Resolution:** 10 nanoseconds per tick
- **Format:** 32-bit counter (good enough for sub-second measurements)

**Shared Memory:**
- **Base Address:** 0x3E000000 (physical)
- **Size:** 8 MB reserved region
- **Device Tree:** Reserved via `reserved-memory` node (not explicitly marked coherent due to CCI-400 limitations)
- **APU Access:** Non-cacheable (O_SYNC flag) to simulate conservative scenario
- **RPU Access:** Cacheable with manual invalidation

**Critical Timing Point:**
```c
// RPU measurement (from rpu_perf_test.c)
Xil_DCacheInvalidateRange((INTPTR)shared_mem, 256);      // Invalidate metadata
packet_size = shared_mem[1];                             // Read size
Xil_DCacheInvalidateRange((INTPTR)&shared_mem[4], size); // Invalidate payload
__asm__ __volatile__("dsb sy" ::: "memory");             // Make sure it completes
rpu_ts = read_timer();  // ← TIMESTAMP HERE (after invalidation, before data read)
```

This captures ONLY the cache management overhead, not data transfer time.

## 🔧 Technical Details

### CCI-400 Status

**Current State:** NOT OPERATIONAL

**Root Cause Analysis:**
1. **PMUFW Limitations:** Standard Xilinx Platform Management Unit Firmware (PMUFW) doesn't initialize CCI-400 clocks and ports for RPU
2. **Missing Prerequisites:**
   - CCI-400 clock gating must be disabled
   - ACE (AXI Coherency Extensions) ports must be configured for Cortex-R5F
   - SCU (Snoop Control Unit) must be enabled for RPU domain
3. **Device Tree Limitations:** `dma-coherent` property present but not effective without PMUFW support

**Attempted Solutions:**
- Modified FSBL hooks (`firmware/fsbl/xfsbl_hooks.c`) to write CCI-400 registers
- Result: Not enough, requires PMUFW rebuild with Xilinx EDK (not accessible in standard flow)

**Conclusion:** Hardware coherence would give significant benefits (30-50% for our workload), but needs custom PMUFW or Xilinx support for enabling CCI-400 for heterogeneous cores.

### Device Tree Configuration

Key device tree nodes (from `linux/device-tree/system_current.dts`):

**Note:** Our tests use 0x3E000000 (not explicitly in DT) since it's free LPDDR4 space. For production, use official reserved regions.

---

## 📚 Additional Resources

### Documentation
- [Zynq UltraScale+ Technical Reference Manual (UG1085)](https://www.xilinx.com/support/documentation/user_guides/ug1085-zynq-ultrascale-trm.pdf)
- [ARM CCI-400 Technical Reference Manual](https://developer.arm.com/documentation/ddi0470/latest/)
- [Kria KR260 Starter Kit User Guide](https://www.xilinx.com/products/som/kria/kr260-robotics-starter-kit.html)

### Related Work
- ARM TrustZone and cache coherence in heterogeneous systems
- OpenAMP framework for AMP communication
- Real-time operating systems on Cortex-R5F

---

## 📄 License

This project is released under the MIT License. See [LICENSE](LICENSE) file for details.

---

## 🤝 Contributing

This is an academic project with completed experimental phase. However, we welcome:
- Bug reports and fixes
- Improved documentation
- Extensions for other Zynq platforms
- Alternative measurement methodologies

Feel free to open an issue or pull request on GitHub.

---

## ✉️ Contact

For questions about this project, reach out to:

- Luigi Caucci: l.caucci@studenti.unina.it
- Simone Cecere: simo.cecere@studenti.unina.it

---
**Last Updated:** November 2025
