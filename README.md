# Zynq UltraScale+ MPSoC Cache Coherence Analysis

This project analyzes cache coherence mechanisms in Xilinx Zynq UltraScale+ MPSoC, with a focus on the CCI-400 hardware coherence between APU (Cortex-A53) and RPU (Cortex-R5F) cores.

## 👥 Authors

- **Luigi Caucci** (M63001679)
- **Simone Cecere** (M63001681)

**Course:** Real Time Systems And Industrial Applications  
**Institution:** Università degli Studi di Napoli Federico II  
**Platform:** Xilinx Kria KR260  
**Academic Year:** 2024/2025

---

## 📖 Project Overview

We're investigating cache coherence in heterogeneous multiprocessor systems—specifically the ARM CCI-400 (Cache Coherent Interconnect) within the Zynq UltraScale+ architecture. The goal was to measure the overhead of manual cache management and estimate what performance gains we could get if the hardware coherence actually worked.

### Key Findings

- **Cache Invalidation Overhead:** Around 1.5-3.5 µs for typical packet sizes in the baseline non-coherent scenario
- **CCI-400 Status:** Turns out it's not operational in the standard Xilinx BSP because the PMUFW doesn't initialize it properly
- **Theoretical Speedup:** We're looking at 30-50% improvement for small packets (<128B) if hardware coherence were functional
- **Measurement Method:** Direct timer-based profiling using TTC0 at 100MHz, giving us 10ns resolution

---

## 🗂️ Repository Structure

```
RealTimeSystemsAndIndustrialApplications/
├── Real_Time_Systems_And_Industrial_Applications___Caucci__Cecere.pttx
├── Real_Time_Systems_And_Industrial_Applications___Caucci__Cecere.pdf
├── README.md                    # Main project documentation
├── LICENSE                      # MIT License
├── .gitignore                   # Git ignore rules
│
├── firmware/                    # Embedded firmware for MPSoC cores
│   ├── apu/
│   │   └── apu_bare_metal.c    # Bare-metal APU coherence test
│   ├── rpu/
│   │   ├── coherence_test/     # Basic coherence verification
│   │   │   ├── rpu_coherency_test.c
│   │   │   └── lscript.ld      # Linker script
│   │   ├── coherence_test_mod/ # Modified coherence test with monitoring
│   │   │   ├── rpu_coherency_test_mod.c
│   │   │   └── lscript.ld      # Linker script
│   │   └── performance_test/   # Performance measurement firmware
│   │       ├── rpu_receiver_ddr.c  # RPU cache invalidation overhead (DDR)
│   │       └── rpu_receiver_tcm.c  # RPU performance test (TCM)
│   └── fsbl/
│       ├── xfsbl_hooks.c       # FSBL modifications for CCI-400 (experimental)
│       └── README.md           # Explanation of FSBL modifications
│
├── linux/                       # Linux userspace and kernel components
│   ├── applications/
│   │   ├── apu_sender_ddr.c    # APU performance test (DDR shared memory)
│   │   ├── apu_sender_tcm.c    # APU performance test (TCM shared memory)
│   │   ├── apu_coherency_test.c # Simple coherence verification
│   │   └── Makefile            # Build configuration
│   ├── device-tree/
│   │   └── system_current.dts  # Complete device tree (extracted from board)
│   └── kernel-modules/
│       ├── coherency_stress.c  # Kernel-space stress test module
│       └── Makefile            # Kernel module build
│
├── analysis/                    # Data analysis and visualization
│   ├── analyze_performance.py  # Python script for DDR performance analysis
│   ├── compare_tcm_ddr.py      # Comparison between TCM and DDR results
│   └── requirements.txt        # Python dependencies
│
├── docs/                        # Documentation
│   ├── kr260_setup_guide.md    # Hardware setup guide
│
└── scripts/                     # Automation scripts
    ├── setup_experiment.sh     # Experiment environment setup
    ├── build_rpu.sh            # RPU firmware build script
    ├── deploy.sh               # Deploy to target board
    └── run_tests.sh            # Execute performance tests
```


## 🚀 Quick Start

### Prerequisites

**Hardware:**
- Xilinx Kria KR260 Starter Kit (or any equivalent Zynq UltraScale+ board)
- SD card with PetaLinux 2022.2
- USB-UART cable for the debug console
- Network connection (optional, for SSH access)

**Software:**
- Vitis 2022.2 for compiling RPU firmware
- PetaLinux 2022.2 SDK for cross-compilation
- Python 3.8+ with numpy, pandas, matplotlib
- Linux development machine—Ubuntu 20.04+ is recommended

### 1. Build RPU Firmware

```bash
# Using Vitis IDE (recommended approach)
# 1. Open Vitis 2022.2
# 2. Create new application project
# 3. Platform: zynqmp_fsbl_bsp
# 4. Domain: standalone on psu_cortexr5_0
# 5. Import firmware/rpu/performance_test/rpu_perf_test.c
# 6. Build project → this generates the .elf file

# Or use the script if you have Vitis CLI set up
cd scripts
./build_rpu.sh
```

### 2. Build APU Application

```bash
cd linux/applications

# Cross-compile for ARM64
make

# Or do it manually:
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
# Load RPU firmware using Remoteproc
echo <FIRMWARE.elf> > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Give it a couple seconds to initialize
sleep 2

# Run the APU test and generates results.csv
./apu_perf_test <REPETITIONS> <OUTPUT.csv>

# Results get saved to csv
```

### 5. Analyze Results

```bash
# Copy results from board to USB drive
scp /home/root mnt/usb

# Run the analysis script
python3 <ANALYSIS_SCRIPT.py> <OUTPUT.csv> output/

# Generated files:
# - output_comparison.png       : Main comparison plot
# - output_report.txt           : Detailed analysis report
# - output_stats.csv            : Statistical summary
```

---

## 📊 Experimental Methodology

### Test Scenarios

We implemented several different test approaches to isolate various aspects of cache coherence:

#### 1. **Performance Measurement Test** (Primary)
- **Location:** `firmware/rpu/performance_test/` + `linux/applications/apu_sender_ddr.c`
- **Purpose:** Measure cache invalidation overhead in the non-coherent scenario
- **Method:** 
  - APU writes data to shared memory (uncached via O_SYNC)
  - APU signals the RPU using a magic value
  - RPU invalidates cache lines for both metadata and payload
  - RPU takes timestamp immediately after invalidation, before reading any data
  - The delta gives us the time to invalidate cache—this is the overhead that CCI-400 would eliminate
- **Packet Sizes:** 1B, 16B, 32B, 64B, 128B, 256B, 512B, 1KB, 2KB, 4KB, 8KB, 16KB, 32KB, 64KB
- **Iterations:** 100 per size, so 1400 total measurements

#### 2. **Basic Coherence Test** (Verification)
- **Location:** `firmware/rpu/coherence_test/` + `linux/applications/apu_coherency_test.c`
- **Purpose:** Verify basic APU-RPU communication works
- **Method:** Simple ping-pong test with cache enabled but no explicit management
- **Expected Result:** Fails without CCI-400, which demonstrates why we need manual cache operations

#### 3. **Kernel Module Stress Test** (Advanced)
- **Location:** `linux/kernel-modules/coherency_stress.c`
- **Purpose:** High-frequency write test from kernel space
- **Method:**
  - Kernel module writes alternating patterns (OLD/NEW) without flushing cache
  - RPU reads continuously and counts pattern occurrences
  - If the NEW pattern is visible → coherence is working
  - If we only see the OLD pattern → no coherence (RPU is seeing stale DRAM)
- **Expected Result:** Confirms CCI-400 isn't operational (0% NEW pattern detection)

### Measurement Details

**Timer Configuration:**
- **Timer:** TTC0 (Triple Timer Counter 0) at 0xFF110000
- **Frequency:** 100 MHz
- **Resolution:** 10 nanoseconds per tick
- **Format:** 32-bit counter, which is good enough for sub-second measurements

**Shared Memory:**
- **Base Address:** 0x3E000000 (physical)
- **Size:** 8 MB reserved region
- **Device Tree:** Reserved via `reserved-memory` node (not explicitly marked coherent because of the CCI-400 limitations)
- **APU Access:** Non-cacheable (O_SYNC flag) to simulate a conservative scenario
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

This captures only the cache management overhead, not the actual data transfer time.

## 🔧 Technical Details

### CCI-400 Status

**Current State:** NOT OPERATIONAL

**Root Cause Analysis:**

The standard Xilinx Platform Management Unit Firmware (PMUFW) just doesn't initialize the CCI-400 clocks and ports for the RPU. We're missing several prerequisites:
- CCI-400 clock gating needs to be disabled
- ACE (AXI Coherency Extensions) ports must be configured for the Cortex-R5F
- SCU (Snoop Control Unit) must be enabled for the RPU domain

The `dma-coherent` property is there in the device tree, but it's not effective without proper PMUFW support.

**Attempted Solutions:**

We tried modifying FSBL hooks (`firmware/fsbl/xfsbl_hooks.c`) to write directly to CCI-400 registers. Result: not enough. You really need to rebuild the PMUFW with Xilinx EDK, which isn't accessible in the standard flow.

**Conclusion:** Hardware coherence would give us significant benefits (30-50% for our workload), but you'd need either a custom PMUFW or Xilinx support to properly enable CCI-400 for heterogeneous cores.

### Device Tree Configuration

Key device tree nodes are in `linux/device-tree/system_current.dts`.

**Note:** Our tests use 0x3E000000, which isn't explicitly in the DT, but it's free LPDDR4 space. For production you'd want to use the official reserved regions.

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

This project is released under the MIT License. See the [LICENSE](LICENSE) file for details.

---

## 🤝 Contributing

This is an academic project and the experimental phase is complete. However, we welcome:
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