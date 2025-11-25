# FSBL Modifications for CCI-400

This directory contains experimental modifications to the First Stage Boot Loader (FSBL) to try and enable CCI-400 hardware cache coherence.

## File: xfsbl_hooks.c

This file provides hooks that run during FSBL initialization, letting us do custom hardware configuration before Linux boots.

### What We Tried

The code attempts to:

1. **Enable CCI-400 Snoop Control**
   ```c
   Xil_Out32(0xFD6E4000U, 0x00000003U);  // CCI-400 Snoop Control S3
   ```
   - Bit 0: Enable snooping
   - Bit 1: Support DVM (Distributed Virtual Memory) operations

2. **Configure RPU for Coherent Access**
   ```c
   rpu0_cfg |= 0x00000002U;  // Set COHERENT bit in RPU_RPU_0_CFG
   Xil_Out32(RPU_RPU_0_CFG, rpu0_cfg);
   ```

3. **Set Broadcast Mode for APU**
   ```c
   Xil_Out32(0xFF41A040U, 0x00000003U);  // LPD_SLCR_LPD_APU
   ```
   - Enables inner and outer shareability broadcast

### Current Status

**COMMENTED OUT** - These modifications are currently disabled (lines 158-168).

### Why It Doesn't Work

These FSBL modifications are **not enough** to enable CCI-400 because:

1. **PMUFW Dependency**: The Platform Management Unit Firmware (PMUFW) controls clock gating and power domains. CCI-400 clocks might be gated by PMUFW before FSBL even runs.

2. **ACE Port Configuration**: CCI-400 needs proper ACE (AXI Coherency Extensions) port setup, which is usually done by PMUFW or earlier boot firmware.

3. **Register Access Timing**: Writing to CCI-400 registers during FSBL might be too late if hardware initialization already happened earlier in boot.

4. **Missing Prerequisites**: Other system-level stuff (interconnect routing, clock tree setup) is probably required too.

### What Would Actually Work

To fully enable CCI-400, we'd need:

1. **Custom PMUFW** with:
   - CCI-400 clock enable
   - ACE port configuration for RPU
   - Proper power domain sequencing

2. **Modified Boot Flow**:
   - FSBL writes actually verified to reach hardware
   - Correct register addresses and bit fields double-checked
   - Timing of writes confirmed via hardware debug

3. **Xilinx Support**: Access to PMUFW source code and rebuild tools (needs NDA and EDK license)

### Alternative Approach

Since we can't modify PMUFW in the standard Xilinx flow, current best practice is:

- **Explicit cache management** (flush/invalidate) in software
- **Non-coherent programming model** with manual synchronization
- Document the overhead and compare to theoretical coherent performance

### References

- Zynq UltraScale+ TRM (UG1085), Chapter 13: Cache Coherent Interconnect
- ARM CCI-400 TRM: Register descriptions
- Xilinx AR# 71948: CCI-400 configuration for heterogeneous cores

---

**Note:** This code is here to show what we tried and explain why the standard approach doesn't work.