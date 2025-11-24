# Guida Setup Kria KR260 per Esperimenti Cache Coherence

Questa guida descrive come configurare la Xilinx Kria KR260 Starter Kit per eseguire gli esperimenti di cache coherence.

---

## 📦 Requisiti Hardware

### Componenti Necessari

1. **Xilinx Kria KR260 Starter Kit**
   - Som Kria K26 (Zynq UltraScale+ MPSoC XCZU5EV)
   - Carrier Card KR260
   - Alimentatore 12V/3A incluso

2. **Accessori**
   - microSD card (16GB o superiore, classe 10)
   - Cavo Ethernet (connessione di rete)
   - Cavo USB-A to micro-USB (console seriale)
   - PC/Laptop con Linux (Ubuntu 20.04+ raccomandato)

### Specifiche Hardware Rilevanti

```
CPU (APU):
- 4× ARM Cortex-A53 @ 1.33 GHz
- 32KB I-cache + 32KB D-cache (L1) per core
- 1MB L2 cache condivisa
- MMU con supporto TrustZone

CPU (RPU):
- 2× ARM Cortex-R5F @ 533 MHz
- 32KB I-cache + 32KB D-cache (L1) per core
- 128KB TCM per core (Tightly-Coupled Memory)
- MPU per protezione memoria

Memoria:
- 4GB LPDDR4 @ 2400 MT/s
- Bandwidth teorico: ~38 GB/s

Interconnect:
- ARM CCI-400 (Cache Coherent Interconnect)
  * Bus width: 128-bit
  * Frequenza: 533 MHz (stimata)
  * Supporto ACE (AXI Coherency Extensions)
  * 4× ACE master ports, 2× ACE-Lite ports
```

---

## 💿 Preparazione SD Card

### Opzione 1: Immagine Precompilata Ubuntu (Raccomandato)

1. **Download immagine Canonical Ubuntu per Kria**
   ```bash
   # Vai su: https://ubuntu.com/download/xilinx
   # Scarica: ubuntu-22.04.x-iot-kria-classic-k26-som-XX.img.xz
   ```

2. **Flash su SD card**
   ```bash
   # Su Linux
   xzcat ubuntu-22.04.x-iot-kria-k26-som.img.xz | sudo dd of=/dev/sdX bs=4M status=progress
   sync
   
   # Su Windows: usare Balena Etcher
   # https://www.balena.io/etcher/
   ```

3. **Prima configurazione**
   - Inserire SD in slot KR260
   - Connettere UART (115200 8N1)
   - Boot (username/password default: ubuntu/ubuntu)
   - Completare setup iniziale

### Opzione 2: Build Custom con PetaLinux

Per modifiche al device tree o kernel:

1. **Setup ambiente PetaLinux 2022.2**
   ```bash
   # Installare PetaLinux Tools 2022.2
   # https://www.xilinx.com/support/download/index.html/content/xilinx/en/downloadNav/embedded-design-tools.html
   
   source /tools/Xilinx/PetaLinux/2022.2/settings.sh
   ```

2. **Creare progetto**
   ```bash
   petalinux-create -t project --template zynqMP -n kr260_project
   cd kr260_project
   
   # Importare BSP ufficiale KR260 se disponibile
   # petalinux-config --get-hw-description=path/to/kria-kr260.xsa
   ```

3. **Configurare kernel**
   ```bash
   petalinux-config -c kernel
   # Abilitare:
   # - CONFIG_REMOTEPROC=y
   # - CONFIG_ZYNQMP_R5_REMOTEPROC=y
   # - CONFIG_RPMSG=y
   # - CONFIG_VIRTIO=y
   ```

4. **Aggiungere device tree custom**
   ```bash
   # Copiare system-user.dtsi in:
   # project-spec/meta-user/recipes-bsp/device-tree/files/
   
   cp /path/to/system-user.dtsi \
      project-spec/meta-user/recipes-bsp/device-tree/files/
   ```

5. **Build e deploy**
   ```bash
   petalinux-build
   petalinux-package --boot --fsbl --fpga --u-boot --force
   
   # Files generati in images/linux/:
   # - boot.scr, BOOT.BIN, image.ub, boot.bin
   # Copiare su partition 1 della SD card
   ```

---

## 🔌 Connessioni Hardware

### Setup Fisico

```
┌─────────────────────────────────────┐
│   PC/Laptop (Ubuntu)                │
│                                     │
│   USB ──────────────┐              │
│   Ethernet ────────┐│              │
└────────────────────┼┼──────────────┘
                     ││
                     ││ USB-micro (UART)
                     ││ Ethernet
                     ││
              ┌──────▼▼──────────────┐
              │  Kria KR260           │
              │  ┌─────────────────┐  │
              │  │ K26 SOM         │  │
              │  │ (MPSoC)         │  │
              │  └─────────────────┘  │
              │                       │
              │  microSD card ────>   │
              │  12V Power ──────>    │
              └───────────────────────┘
```

### Connessione Seriale

1. **Connettere cavo USB-micro**
   - Porta J4 su KR260
   - 4× porte seriali virtuali appaiono su PC

2. **Identificare porte**
   ```bash
   ls -l /dev/ttyUSB*
   # Tipicamente:
   # /dev/ttyUSB0 → JTAG UART (non usata)
   # /dev/ttyUSB1 → System Controller
   # /dev/ttyUSB2 → Linux console (APU) ← QUESTA
   # /dev/ttyUSB3 → (non usata)
   ```

3. **Aprire console**
   ```bash
   # Usando screen
   screen /dev/ttyUSB2 115200
   
   # Usando minicom
   minicom -D /dev/ttyUSB2 -b 115200
   
   # Usando PuTTY (GUI)
   # Serial: /dev/ttyUSB2
   # Speed: 115200
   # Data bits: 8, Parity: None, Stop bits: 1
   ```

### Connessione di Rete

1. **Configurazione IP statico (raccomandato)**
   ```bash
   # Su KR260 (via UART)
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

2. **Test connessione da PC**
   ```bash
   ping 192.168.1.100
   ssh ubuntu@192.168.1.100  # o root se configurato
   ```

---

## ⚙️ Configurazione Software

### 1. Verificare Remoteproc

```bash
# Su KR260
ls -l /sys/class/remoteproc/
# Dovrebbe mostrare: remoteproc0 → zynqmp_r5_remoteproc

cat /sys/class/remoteproc/remoteproc0/name
# Output: zynqmp_r5_remoteproc

cat /sys/class/remoteproc/remoteproc0/state
# Output: offline (o running se RPU già caricato)
```

Se `remoteproc0` non esiste:

```bash
# Verificare modulo kernel
lsmod | grep remoteproc
# Dovrebbe mostrare: zynqmp_r5_remoteproc

# Se non presente, caricare manualmente
sudo modprobe zynqmp_r5_remoteproc
```

### 2. Configurare Memoria Condivisa

```bash
# Verificare reserved memory nel device tree
cat /proc/iomem | grep rproc
# Output atteso:
# 3ed00000-3ed3ffff : rproc@3ed00000
# 3ed40000-3ed43fff : rpu0vdev0vring0@3ed40000
# ...

# Verificare flag dma-coherent (se configurato)
find /sys/firmware/devicetree/base -name dma-coherent
# Se presente: indica che coerenza è abilitata nel DT
# NOTA: non garantisce CCI-400 funzionante!
```

### 3. Installare Tool di Sviluppo

```bash
# Su KR260
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    python3 python3-pip \
    device-tree-compiler \
    u-boot-tools \
    kmod
```

### 4. Setup Cross-Compilation (su PC)

```bash
# Download ARM64 toolchain
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Test
aarch64-linux-gnu-gcc --version
```

---

## 🧪 Test Iniziale

### Quick Test: Echo via Remoteproc

1. **Creare firmware minimale**
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

2. **Compilare con Vitis** (su PC con Vitis installato)

3. **Deploy e test**
   ```bash
   # Copy to KR260
   scp test_echo.elf ubuntu@192.168.1.100:/lib/firmware/
   
   # Su KR260
   echo test_echo.elf > /sys/class/remoteproc/remoteproc0/firmware
   echo start > /sys/class/remoteproc/remoteproc0/state
   
   # Visualizzare output RPU
   cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
   # Dovrebbe mostrare: "Hello from RPU!" e "RPU alive"
   ```

### Test Device Tree

```bash
# Dump device tree compilato
dtc -I dtb -O dts /boot/system.dtb -o /tmp/system.dts

# Verificare sezioni chiave
grep -A 20 "reserved-memory" /tmp/system.dts
grep -A 30 "rf5ss" /tmp/system.dts
```

---

## 🔧 Troubleshooting

### Problema: Remoteproc non disponibile

```bash
# Verificare kernel config
zcat /proc/config.gz | grep REMOTEPROC
# Dovrebbe mostrare:
# CONFIG_REMOTEPROC=y
# CONFIG_ZYNQMP_R5_REMOTEPROC=y

# Se non presente, rebuild kernel con opzioni abilitate
```

### Problema: RPU non parte

```bash
# Check dmesg per errori
dmesg | grep -i remoteproc | tail -20

# Errori comuni:
# - "Failed to load firmware" → file .elf non trovato o corrotto
# - "Failed to parse firmware" → .elf non compatibile (wrong arch/platform)
# - "Failed to allocate memory" → reserved-memory non configurata
```

### Problema: Shared memory non accessibile

```bash
# Verificare permessi /dev/mem
ls -l /dev/mem
# Dovrebbe essere: crw------- root root

# Test accesso memoria (con cautela!)
sudo devmem 0x3E000000
# Dovrebbe ritornare valore (anche se random)

# Se fallisce: problema con device tree o MMU
```

### Problema: Network non raggiungibile

```bash
# Verificare interfaccia
ip addr show eth0
# Dovrebbe mostrare IP configurato

# Test link fisico
ethtool eth0
# Link detected: yes

# Firewall
sudo ufw status
# Se active, potrebbe bloccare SSH
```

---

## 📚 Risorse Aggiuntive

### Documentazione Xilinx

- [Kria KR260 Wiki](https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/1641152513/Kria+KR260+Robotics+Starter+Kit)
- [Remoteproc User Guide](https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18841976/RPU+OpenAMP+Communication)
- [Device Tree Guide](https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18842279/Linux+Device+Tree+Generator)

### Community

- [Xilinx Forums](https://support.xilinx.com/s/question/0D52E00006hpNeCCAU/kria-kr260)
- [Kria GitHub](https://github.com/Xilinx/kria-apps-docs)

### Tool Download

- [Vitis 2022.2](https://www.xilinx.com/support/download.html)
- [PetaLinux 2022.2](https://www.xilinx.com/support/download/index.html/content/xilinx/en/downloadNav/embedded-design-tools.html)

---

**Ultimo aggiornamento:** Novembre 2024  
**Testato su:** Kria KR260 (K26 SOM), Ubuntu 22.04, PetaLinux 2022.2