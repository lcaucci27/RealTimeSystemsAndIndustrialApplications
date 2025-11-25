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

## 🗂️ Repository Structure (Corrected)

```
RealTimeSystemsAndIndustrialApplications/
│
├── README.md                     # Project overview and documentation
├── LICENSE                       # MIT License
├── .gitignore                    # Git ignore rules
│
├── firmware/                     # Embedded firmware for MPSoC cores
│   ├── apu/
│   │   ├── apu_bare_metal.c      # Bare-metal APU coherence test
│   │
│   ├── rpu/
│   │   ├── coherence_test/       # Basic coherence verification
│   │   │   ├── lscript.ld
│   │   │   └── rpu_coherency_test.c
│   │   │
│   │   ├── coherence_test_mod/   # Modified coherence test with monitoring
│   │   │   ├── lscript.ld
│   │   │   └── rpu_coherency_test_mod.c
│   │   │
│   │   └── performance_test/     # Performance measurement firmware
│   │       └── rpu_perf_test.c   # Cache invalidation overhead measurement
│   │
│   └── fsbl/
│       ├── xfsbl_hooks.c         # FSBL modifications (experimental)
│       └── README.md             # Explanation of FSBL approach
│
├── linux/                        # Linux userspace + kernel components
│   ├── applications/
│   │   ├── apu_perf_test.c       # APU performance test application
│   │   ├── apu_coherency_test.c  # Simple coherence verification
│   │   └── Makefile              # Cross-compilation rules
│   │
│   ├── device-tree/
│   │   └── system_current.dts    # Device tree extracted from board
│   │
│   └── kernel-modules/
│       ├── coherency_stress.c    # Kernel-space stress test module
│       └── Makefile              # Kernel module build configuration
│
├── analysis/                     # Data analysis and visualization
│   ├── analyze_performance.py    # Python script for statistical analysis
│   └── requirements.txt          # NumPy, Matplotlib, Pandas, etc.
│
├── docs/                         # Documentation
│   └── kr260_setup_guide.md      # Hardware setup and configuration guide
│
├── results/
│   └── results.csv               # Real performance measurements (1401 samples)
│
└── scripts/                      # Automation scripts
    ├── setup_experiment.sh       # Target environment setup
    ├── build_rpu.sh              # Build RPU firmware
    ├── deploy.sh                 # Deploy binaries to the KR260
    ├── run_tests.sh              # Execute the performance test suite
```

---

## 🚀 Quick Start

### Prerequisites

**Hardware:**
- Xilinx Kria KR260 Starter Kit (or equivalent Zynq UltraScale+ board)
- SD card with PetaLinux 2022.2
- USB-UART cable for debug console
- Network connection (SSH access)

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
aarch64-linux-gnu-gcc -O2 -static -o apu_perf_test apu_perf_test.c
```

### 3. Deploy to Board

```bash
# Set your board IP
export BOARD_IP=192.168.1.100

# Deploy firmware and applications
cd scripts
./deploy.sh
```

### 4. Run Performance Tests

```bash
# On the board (via SSH)
ssh root@$BOARD_IP

# Load RPU firmware via Remoteproc
echo rpu_perf_test.elf > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Wait a couple seconds for RPU initialization
sleep 2

# Run APU test (generates results.csv)
./apu_perf_test 100 performance_results.csv

# Results saved to performance_results.csv
```

### 5. Analyze Results

```bash
# Copy results from board to PC
scp root@$BOARD_IP:~/performance_results.csv results/

# Run analysis script
cd analysis
pip install -r requirements.txt
python3 analyze_performance.py ../results/performance_results.csv output/

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
- **Location:** `firmware/rpu/performance_test/` + `linux/applications/apu_perf_test.c`
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

---

## 📈 Results Summary

Based on 1401 real measurements on Kria KR260:

### Measured Performance (Non-Coherent Baseline)

| Packet Size | Mean Latency | Std Dev | Throughput |
|-------------|--------------|---------|------------|
| 1 B         | ~1.8 µs      | ±0.3 µs | N/A        |
| 64 B        | ~2.0 µs      | ±0.4 µs | ~32 MB/s   |
| 256 B       | ~2.5 µs      | ±0.5 µs | ~102 MB/s  |
| 1 KB        | ~4.0 µs      | ±0.8 µs | ~250 MB/s  |
| 64 KB       | ~50 µs       | ±10 µs  | ~1280 MB/s |

**Key Observations:**
- Baseline latency: ~1.5-2.0 µs (dominated by cache invalidation)
- Linear scaling with packet size (cache line granularity)
- Throughput limited by invalidation overhead for small packets

### Theoretical CCI-400 Performance (Model)

Assuming functional hardware coherence:
- **Small packets (<128B):** 30-50% speedup (eliminate ~1 µs invalidation overhead)
- **Large packets (>4KB):** 10-20% speedup (data transfer dominates)
- **Mechanism:** Cache-to-cache transfer via CCI snoop, no manual invalidation

**Model Parameters:**
- CCI-400 frequency: 533 MHz (estimated from PS clock tree)
- Snoop latency: ~30 cycles (from ARM CCI-400 spec)
- Cache-to-cache transfer: ~40 cycles for 64B line
- No software overhead for explicit cache ops

---

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

```dts
reserved-memory {
    rproc@3ed00000 {
        reg = <0x0 0x3ed00000 0x0 0x40000>;  // 256KB for firmware
    };
    rpu0vdev0buffer@3ed48000 {
        reg = <0x0 0x3ed48000 0x0 0x100000>; // 1MB VirtIO buffer
    };
    rpu_shmem@70000000 {
        compatible = "shared-dma-pool";
        reg = <0x0 0x70000000 0x0 0x1000000>; // 16MB DMA pool
        dma-coherent;  // Coherence flag (not functional)
    };
};

&rf5ss {
    compatible = "xlnx,zynqmp-r5-remoteproc";
    dma-coherent;  // Inherited by R5F cores (doesn't work without PMUFW)
    r5f_0 {
        compatible = "xilinx,r5f";
        power-domain = <&zynqmp_firmware 7>;
    };
};
```

**Note:** Our tests use 0x3E000000 (not explicitly in DT) since it's free LPDDR4 space. For production, use official reserved regions.

---

## 🛠️ Troubleshooting

### RPU Not Starting

```bash
# Check Remoteproc status
cat /sys/class/remoteproc/remoteproc0/state

# Check logs
dmesg | grep remoteproc

# Common issues:
# - Firmware not in /lib/firmware/
# - Wrong firmware name
# - Previous RPU instance still running (echo stop > state first)
```

### No Results Generated

```bash
# Check if RPU is receiving packets
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0

# Verify shared memory accessible
devmem 0x3E000000

# Check timer is running (on board)
devmem 0xFF110018  # Should be incrementing
```

### Python Analysis Errors

```bash
# Make sure dependencies are installed
pip install -r analysis/requirements.txt

# Check CSV format
head results/results.csv
# Should show: packet_size,apu_timestamp,rpu_timestamp,delta_ticks,delta_us

# Verify Python version
python3 --version  # Needs 3.8+
```

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
```