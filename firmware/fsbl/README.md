# FSBL Modifications for CCI-400

This directory contains experimental modifications to the First Stage Boot Loader (FSBL) to enable CCI-400 hardware cache coherence.

## File: xfsbl_hooks.c

This file provides hooks that execute during FSBL initialization, allowing custom hardware configuration before Linux boot.

### Attempted Configuration

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

These FSBL modifications are **insufficient** to enable CCI-400 because:

1. **PMUFW Dependency**: The Platform Management Unit Firmware (PMUFW) controls clock gating and power domains. CCI-400 clocks may be gated by PMUFW before FSBL runs.

2. **ACE Port Configuration**: The CCI-400 requires proper ACE (AXI Coherency Extensions) port configuration, which is typically done by PMUFW or early boot firmware.

3. **Register Access Timing**: Writing to CCI-400 registers during FSBL may be too late if hardware initialization happens earlier in the boot sequence.

4. **Missing Prerequisites**: Other system-level configurations (interconnect routing, clock tree setup) may be required.

### Required Solution

To fully enable CCI-400, one would need:

1. **Custom PMUFW** with:
   - CCI-400 clock enable
   - ACE port configuration for RPU
   - Proper power domain sequencing

2. **Modified Boot Flow**:
   - FSBL writes validated to actually reach hardware
   - Correct register addresses and bit fields verified
   - Timing of writes confirmed via hardware debug

3. **Xilinx Support**: Access to PMUFW source code and rebuild tools (requires NDA and EDK license)

### Alternative Approach

Since PMUFW modifications are not accessible in standard Xilinx flow, current best practice is:

- **Explicit cache management** (flush/invalidate) in software
- **Non-coherent programming model** with manual synchronization
- Document the overhead and compare to theoretical coherent performance

### References

- Zynq UltraScale+ TRM (UG1085), Chapter 13: Cache Coherent Interconnect
- ARM CCI-400 TRM: Register descriptions
- Xilinx AR# 71948: CCI-400 configuration for heterogeneous cores

---

**Note:** This code is provided for educational purposes to demonstrate the attempted approach and explain why standard solutions are insufficient.