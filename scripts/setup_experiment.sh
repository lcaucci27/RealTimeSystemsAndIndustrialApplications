set -e

# Basic config, adjust these if your setup is different
SHMEM_BASE=0x3E000000
SHMEM_SIZE=0x00800000
TIMER_BASE=0xFF250000
RPU_FIRMWARE="rpu_receiver_io.elf"
FIRMWARE_PATH="/lib/firmware"
REMOTEPROC_PATH="/sys/class/remoteproc/remoteproc0"

echo "========================================="
echo "APU-RPU Performance Experiment Setup"
echo "========================================="
echo ""

# Need root to mess with hardware registers and remoteproc
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root"
    echo "Usage: sudo ./setup_experiment.sh"
    exit 1
fi

# Step 1: Check device tree setup
echo "Step 1: Verifying device tree configuration..."
if [ -d "/sys/firmware/devicetree/base/reserved-memory" ]; then
    echo "  Reserved memory nodes:"
    ls -la /sys/firmware/devicetree/base/reserved-memory/ | grep -E "rproc|rpu"
else
    echo "  WARNING: reserved-memory not found in device tree"
fi

# This property should be there if coherence is configured (even though it doesn't work)
if [ -f "/sys/firmware/devicetree/base/rf5ss@ff9a0000/r5f_0/dma-coherent" ]; then
    echo "  dma-coherent property: PRESENT"
else
    echo "  WARNING: dma-coherent property not found"
fi
echo ""

# Step 2: Check remoteproc is available
echo "Step 2: Verifying remoteproc..."
if [ -d "$REMOTEPROC_PATH" ]; then
    STATE=$(cat $REMOTEPROC_PATH/state)
    echo "  Remoteproc state: $STATE"
    
    # If RPU is already running, stop it first
    if [ "$STATE" = "running" ]; then
        echo "  Stopping current RPU firmware..."
        echo stop > $REMOTEPROC_PATH/state
        sleep 1
    fi
else
    echo "  ERROR: Remoteproc not found at $REMOTEPROC_PATH"
    echo "  Please ensure OpenAMP/remoteproc is configured"
    exit 1
fi
echo ""

# Step 3: Copy firmware to where remoteproc expects it
echo "Step 3: Installing RPU firmware..."
if [ -f "$RPU_FIRMWARE" ]; then
    cp "$RPU_FIRMWARE" "$FIRMWARE_PATH/"
    echo "  Copied $RPU_FIRMWARE to $FIRMWARE_PATH/"
    
    # Tell remoteproc which firmware to load
    echo "$RPU_FIRMWARE" > $REMOTEPROC_PATH/firmware
    echo "  Set firmware name in remoteproc"
else
    echo "  WARNING: $RPU_FIRMWARE not found in current directory"
    echo "  Please copy the compiled ELF to current directory"
fi
echo ""

# Step 4: Make sure we can access the timer
echo "Step 4: Verifying timer access..."
TIMER_CTRL=$(devmem $TIMER_BASE 32 2>/dev/null || echo "ERROR")
if [ "$TIMER_CTRL" != "ERROR" ]; then
    echo "  Timer control register: $TIMER_CTRL"
    
    # Bit 0 enables the timer
    TIMER_VAL=$(printf "%d" "$TIMER_CTRL")
    if [ $((TIMER_VAL & 1)) -eq 1 ]; then
        echo "  Timer is ENABLED"
    else
        echo "  WARNING: Timer is DISABLED"
        echo "  Enabling timer..."
        devmem $TIMER_BASE 32 $((TIMER_VAL | 1))
    fi
else
    echo "  ERROR: Cannot access timer registers"
    echo "  This might indicate permission issues"
fi
echo ""

# Step 5: Try to compile APU code if gcc is available on the board
echo "Step 5: Checking APU sender compilation..."
if command -v gcc &> /dev/null; then
    if [ -f "apu_sender_ttc.c" ]; then
        echo "  Compiling apu_sender_ttc.c..."
        gcc -O2 -o apu_sender_ttc apu_sender_ttc.c -lrt
        echo "  Compiled successfully"
        chmod +x apu_sender_ttc
    else
        echo "  apu_sender_ttc.c not found in current directory"
    fi
else
    echo "  WARNING: gcc not available on target"
    echo "  Please cross-compile apu_sender_ttc.c on host:"
    echo "  aarch64-linux-gnu-gcc -O2 -o apu_sender_ttc apu_sender_ttc.c -lrt"
fi
echo ""

# Step 6: Quick sanity check that shared memory is accessible
echo "Step 6: Checking shared memory region..."
echo "  Attempting to read from 0x$SHMEM_BASE..."
SHMEM_TEST=$(devmem $SHMEM_BASE 32 2>/dev/null || echo "ERROR")
if [ "$SHMEM_TEST" != "ERROR" ]; then
    echo "  Shared memory accessible: $SHMEM_TEST"
else
    echo "  ERROR: Cannot access shared memory"
    echo "  Check device tree and memory mapping"
fi
echo ""

# Step 7: Fire up the RPU
echo "Step 7: Starting RPU firmware..."
if [ -f "$FIRMWARE_PATH/$RPU_FIRMWARE" ]; then
    echo "  Starting remoteproc..."
    echo start > $REMOTEPROC_PATH/state
    sleep 2
    
    NEW_STATE=$(cat $REMOTEPROC_PATH/state)
    echo "  Remoteproc state: $NEW_STATE"
    
    if [ "$NEW_STATE" = "running" ]; then
        echo "  RPU firmware started successfully!"
    else
        echo "  ERROR: Failed to start RPU firmware"
        echo "  Check dmesg for errors"
    fi
else
    echo "  Skipping RPU start (firmware not installed)"
fi
echo ""

# Step 8: Remind about debug output
echo "Step 8: RPU Console Output"
echo "  You can view RPU output via UART console (if connected)"
echo "  Or check: dmesg | grep -i rpu"
echo ""

# Quick summary of what we set up
echo "========================================="
echo "Setup Summary"
echo "========================================="
echo "Shared Memory Base: 0x$SHMEM_BASE"
echo "Shared Memory Size: $SHMEM_SIZE"
echo "Timer Base:         0x$TIMER_BASE"
echo "Remoteproc:         $REMOTEPROC_PATH"
echo "RPU Firmware:       $RPU_FIRMWARE"
echo ""

if [ -f "apu_sender_ttc" ] && [ -f "$FIRMWARE_PATH/$RPU_FIRMWARE" ]; then
    echo "To run the experiment:"
    echo "  1. Connect to RPU UART console (optional, for debugging)"
    echo "  2. Run: sudo ./apu_sender_ttc 100 results.csv"
    echo "  3. Wait for experiment to complete"
    echo "  4. Analyze results: python3 analyze_performance.py results.csv"
else
    echo "Missing components:"
    [ ! -f "apu_sender_ttc" ] && echo "  - apu_sender_ttc binary"
    [ ! -f "$FIRMWARE_PATH/$RPU_FIRMWARE" ] && echo "  - RPU firmware in $FIRMWARE_PATH"
fi

echo ""
echo "Done!"