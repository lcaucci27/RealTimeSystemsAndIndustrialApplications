# KR260 Setup Guide for Cache Coherence Experiments

This guide walks through setting up the Xilinx Kria KR260 Starter Kit to run the cache coherence experiments.

---

## 📦 Hardware Requirements

### What You Need

1. **Xilinx Kria KR260 Starter Kit**
   - Kria K26 SOM (Zynq UltraScale+ MPSoC XCZU5EV)
   - KR260 Carrier Card
   - 12V/3A power adapter (included)

2. **Accessories**
   - microSD card (16GB or larger, class 10)
   - Ethernet cable for network connection
   - USB-A to micro-USB cable for serial console
   - PC/Laptop with Linux—Ubuntu 20.04+ is recommended

### Relevant Hardware Specs

```
CPU (APU):
- 4× ARM Cortex-A53 @ 1.33 GHz
- 32KB I-cache + 32KB D-cache (L1) per core
- 1MB shared L2 cache
- MMU with TrustZone support

CPU (RPU):
- 2× ARM Cortex-R5F @ 533 MHz
- 32KB I-cache + 32KB D-cache (L1) per core
- 128KB TCM per core (Tightly-Coupled Memory)
- MPU for memory protection

Memory:
- 4GB LPDDR4 @ 2400 MT/s
- Theoretical bandwidth: ~38 GB/s

Interconnect:
- ARM CCI-400 (Cache Coherent Interconnect)
  * Bus width: 128-bit
  * Frequency: 533 MHz (estimated)
  * ACE support (AXI Coherency Extensions)
  * 4× ACE master ports, 2× ACE-Lite ports
```

---

## 💿 SD Card Preparation

### Option 1: Pre-built Ubuntu Image

1. **Download Canonical Ubuntu for Kria**
   ```bash
   # Go to: https://ubuntu.com/download/xilinx
   # Download: ubuntu-22.04.x-iot-kria-classic-k26-som-XX.img.xz
   ```

2. **Flash to SD card**
   ```bash
   # On Linux
   xzcat ubuntu-22.04.x-iot-kria-k26-som.img.xz | sudo dd of=/dev/sdX bs=4M status=progress
   sync
   
   # On Windows: use Balena Etcher
   # https://www.balena.io/etcher/
   ```

3. **Initial setup**
   - Insert SD into KR260 slot
   - Connect UART (115200 8N1)
   - Boot—default username/password is ubuntu/ubuntu
   - Complete initial setup

### Option 2: Custom Build with PetaLinux (Recommended)

If you need device tree or kernel modifications, this is the way to go:

1. **Setup PetaLinux 2022.2 environment**
   ```bash
   # Install PetaLinux Tools 2022.2
   # https://www.xilinx.com/support/download/index.html/content/xilinx/en/downloadNav/embedded-design-tools.html
   
   source /tools/Xilinx/PetaLinux/2022.2/settings.sh
   ```

2. **Create project**
   ```bash
   petalinux-create -t project --template zynqMP -n kr260_project
   cd kr260_project
   
   # Import official KR260 BSP if available
   # petalinux-config --get-hw-description=path/to/kria-kr260.xsa
   ```

3. **Configure kernel**
   ```bash
   petalinux-config -c kernel
   # Enable:
   # - CONFIG_REMOTEPROC=y
   # - CONFIG_ZYNQMP_R5_REMOTEPROC=y
   # - CONFIG_RPMSG=y
   # - CONFIG_VIRTIO=y
   ```

4. **Add custom device tree**
   ```bash
   # Copy system-user.dtsi to:
   # project-spec/meta-user/recipes-bsp/device-tree/files/
   
   cp /path/to/system-user.dtsi \
      project-spec/meta-user/recipes-bsp/device-tree/files/
   ```

5. **Build and deploy**
   ```bash
   petalinux-build
   petalinux-package --boot --fsbl --fpga --u-boot --force
   
   # Generated files in images/linux/:
   # - boot.scr, BOOT.BIN, image.ub, boot.bin
   # Copy to partition 1 of SD card
   ```

---

## 🔌 Connecting to the Board

### Serial Connection

1. **Open console**
   ```bash
   # Using screen
   screen /dev/ttyUSB2 115200
   
   # Using minicom
   minicom -D /dev/ttyUSB2 -b 115200
   
   # Using PuTTY (GUI)
   # Serial: /dev/ttyUSB2
   # Speed: 115200
   # Data bits: 8, Parity: None, Stop bits: 1
   ```

### Network Connection

1. **Static IP configuration (recommended)**
   ```bash
   # On KR260 (via UART)
   sudo nano /etc/netplan/01-netcfg.yaml
   ```
   
   ```yaml
   network:
     version: 2
     renderer: networkd
     ethernets:
       eth0:
         dhcp4: no
         addresses:
           - 192.168.1.100/24
         gateway4: 192.168.1.1
         nameservers:
           addresses: [8.8.8.8, 8.8.4.4]
   ```
   
   ```bash
   sudo netplan apply
   ```

2. **Test connection from PC**
   ```bash
   ping 192.168.1.100
   ssh ubuntu@192.168.1.100  # or root if configured
   ```

---

## ⚙️ Software Configuration

### 1. Check Remoteproc

```bash
# On KR260
ls -l /sys/class/remoteproc/
# Should show: remoteproc0 → zynqmp_r5_remoteproc

cat /sys/class/remoteproc/remoteproc0/name
# Output: zynqmp_r5_remoteproc

cat /sys/class/remoteproc/remoteproc0/state
# Output: offline (or running if RPU already loaded)
```

If `remoteproc0` doesn't exist:

```bash
# Check kernel module
lsmod | grep remoteproc
# Should show: zynqmp_r5_remoteproc

# If not present, load manually
sudo modprobe zynqmp_r5_remoteproc
```

### 2. Configure Shared Memory

```bash
# Check reserved memory in device tree
cat /proc/iomem | grep rproc
# Expected output:
# 3ed00000-3ed3ffff : rproc@3ed00000
# 3ed40000-3ed43fff : rpu0vdev0vring0@3ed40000
# ...

# Check dma-coherent flag (if configured)
find /sys/firmware/devicetree/base -name dma-coherent
# If present: indicates coherence is enabled in DT
# NOTE: doesn't guarantee CCI-400 is working!
```

### 3. Install Development Tools

```bash
# On KR260
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    python3 python3-pip \
    device-tree-compiler \
    u-boot-tools \
    kmod
```

### 4. Setup Cross-Compilation (on PC)

```bash
# Download ARM64 toolchain
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Test
aarch64-linux-gnu-gcc --version
```

---

## 🧪 Initial Testing

### Quick Test: Echo via Remoteproc

1. **Create minimal firmware**
   ```c
   // test_echo.c
   #include "xil_printf.h"
   
   int main(void) {
       xil_printf("Hello from RPU!\r\n");
       while(1) {
           for(volatile int i=0; i<10000000; i++);
           xil_printf("RPU alive\r\n");
       }
       return 0;
   }
   ```

2. **Compile with Vitis** (on PC with Vitis installed)

3. **Deploy and test**
   ```bash
   # Copy to KR260
   scp test_echo.elf ubuntu@192.168.1.100:/lib/firmware/
   
   # On KR260
   echo test_echo.elf > /sys/class/remoteproc/remoteproc0/firmware
   echo start > /sys/class/remoteproc/remoteproc0/state
   
   # View RPU output
   cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
   # Should show: "Hello from RPU!" and "RPU alive"
   ```

### Device Tree Test

```bash
# Dump compiled device tree
dtc -I dtb -O dts /boot/system.dtb -o /tmp/system.dts

# Check key sections
grep -A 20 "reserved-memory" /tmp/system.dts
grep -A 30 "rf5ss" /tmp/system.dts
```

---

## 📚 Additional Resources

### Xilinx Documentation

- [Kria KR260 Wiki](https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/1641152513/Kria+KR260+Robotics+Starter+Kit)
- [Remoteproc User Guide](https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18841976/RPU+OpenAMP+Communication)
- [Device Tree Guide](https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18842279/Linux+Device+Tree+Generator)

### Community

- [Xilinx Forums](https://support.xilinx.com/s/question/0D52E00006hpNeCCAU/kria-kr260)
- [Kria GitHub](https://github.com/Xilinx/kria-apps-docs)

### Tool Downloads

- [Vitis 2022.2](https://www.xilinx.com/support/download.html)
- [PetaLinux 2022.2](https://www.xilinx.com/support/download/index.html/content/xilinx/en/downloadNav/embedded-design-tools.html)

---

**Last Updated:** November 2025  
**Tested on:** Kria KR260 (K26 SOM), Ubuntu 22.04, PetaLinux 2022.2