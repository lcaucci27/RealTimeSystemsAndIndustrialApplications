#!/bin/bash
#
# RPU firmware build script using Vitis
#
# Compiles RPU firmware for Zynq UltraScale+ MPSoC.
# Needs Vitis 2022.2 installed and set up properly.
#
# Usage: ./build_rpu.sh [firmware_name]
#   firmware_name: Optional, defaults to "rpu_perf_test"
#                  Options: rpu_perf_test, rpu_coherency_test, rpu_coherency_test_mod
#

set -e  # Bail if anything fails

# Config - adjust these if your Vitis is installed elsewhere
VITIS_VERSION="2022.2"
VITIS_INSTALL_DIR="/tools/Xilinx/Vitis/${VITIS_VERSION}"
WORKSPACE_DIR="$(pwd)/../vitis_workspace"
PLATFORM_NAME="kr260"
DOMAIN_NAME="standalone_r5_0"

# Which firmware to build
FIRMWARE_NAME="${1:-rpu_perf_test}"

# Map firmware name to source directory
case "$FIRMWARE_NAME" in
    "rpu_perf_test")
        SOURCE_DIR="$(pwd)/../firmware/rpu/performance_test"
        SOURCE_FILE="rpu_perf_test.c"
        ;;
    "rpu_coherency_test")
        SOURCE_DIR="$(pwd)/../firmware/rpu/coherence_test"
        SOURCE_FILE="rpu_coherency_test.c"
        ;;
    "rpu_coherency_test_mod")
        SOURCE_DIR="$(pwd)/../firmware/rpu/coherence_mod_test"
        SOURCE_FILE="rpu_coherency_test_mod.c"
        ;;
    *)
        echo "Error: Unknown firmware name: $FIRMWARE_NAME"
        echo "Valid options: rpu_perf_test, rpu_coherency_test, rpu_coherency_test_mod"
        exit 1
        ;;
esac

echo "========================================="
echo "RPU Firmware Build Script"
echo "========================================="
echo "Firmware:  $FIRMWARE_NAME"
echo "Source:    $SOURCE_DIR"
echo "Vitis:     $VITIS_VERSION"
echo "========================================="
echo ""

# Make sure Vitis is actually installed
if [ ! -d "$VITIS_INSTALL_DIR" ]; then
    echo "ERROR: Vitis not found at $VITIS_INSTALL_DIR"
    echo ""
    echo "Please install Vitis $VITIS_VERSION or update VITIS_INSTALL_DIR"
    echo "Download from: https://www.xilinx.com/support/download.html"
    exit 1
fi

# Load Vitis environment variables
echo "Sourcing Vitis environment..."
source "${VITIS_INSTALL_DIR}/settings64.sh"

# Check source file exists
if [ ! -f "${SOURCE_DIR}/${SOURCE_FILE}" ]; then
    echo "ERROR: Source file not found: ${SOURCE_DIR}/${SOURCE_FILE}"
    exit 1
fi

echo "Source file found: ${SOURCE_FILE}"
echo ""

# Instructions for manual build in Vitis IDE
# (automated CLI build is a pain and doesn't always work)
echo "========================================="
echo "Vitis IDE Build Instructions"
echo "========================================="
echo ""
echo "Automatic CLI build is tricky. Here's how to do it in the IDE:"
echo ""
echo "1. Launch Vitis:"
echo "   $ vitis -workspace ${WORKSPACE_DIR}"
echo ""
echo "2. Create Platform (if you haven't already):"
echo "   - File → New → Platform Project"
echo "   - Name: ${PLATFORM_NAME}"
echo "   - Hardware: Browse to your .xsa file from Vivado"
echo ""
echo "3. Create Application Project:"
echo "   - File → New → Application Project"
echo "   - Name: ${FIRMWARE_NAME}"
echo "   - Platform: ${PLATFORM_NAME}"
echo "   - Domain: Create new 'standalone' domain"
echo "   - Processor: psu_cortexr5_0"
echo "   - Template: Empty Application"
echo ""
echo "4. Import Source Code:"
echo "   - Right-click on 'src' folder in ${FIRMWARE_NAME}"
echo "   - Import → General → File System"
echo "   - Browse to: ${SOURCE_DIR}"
echo "   - Select: ${SOURCE_FILE}"
echo "   - Finish"
echo ""
echo "5. Configure BSP:"
echo "   - Expand ${FIRMWARE_NAME}_system → ${DOMAIN_NAME}"
echo "   - Double-click 'Board Support Package'"
echo "   - Overview → Modify BSP Settings"
echo "   - Enable required libraries (xilstandalone, xilffs if needed)"
echo "   - Click OK"
echo ""
echo "6. Build Project:"
echo "   - Project → Build Project"
echo "   - Or right-click ${FIRMWARE_NAME} → Build Project"
echo ""
echo "7. Output ELF will be at:"
echo "   ${WORKSPACE_DIR}/${FIRMWARE_NAME}/Debug/${FIRMWARE_NAME}.elf"
echo ""
echo "8. Copy to firmware directory:"
echo "   cp ${WORKSPACE_DIR}/${FIRMWARE_NAME}/Debug/${FIRMWARE_NAME}.elf \\"
echo "      ${SOURCE_DIR}/${FIRMWARE_NAME}.elf"
echo ""
echo "========================================="
echo ""

# Ask if we should try the CLI build anyway
read -p "Attempt automatic build with Vitis CLI? (y/n) " -n 1 -r
echo ""
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Attempting Vitis CLI build (experimental)..."
    echo "NOTE: This requires pre-configured workspace and platform"
    
    # Try using xsct (Xilinx Software Command Tool)
    xsct << EOF
# Point to workspace
setws ${WORKSPACE_DIR}

# Create or open the application
app create -name ${FIRMWARE_NAME} -platform ${PLATFORM_NAME} -domain ${DOMAIN_NAME}

# Import source files
importsources -name ${FIRMWARE_NAME} -path ${SOURCE_DIR}

# Build the application
app build -name ${FIRMWARE_NAME}

# Done
exit
EOF

    if [ $? -eq 0 ]; then
        echo ""
        echo "Build successful!"
        OUTPUT_ELF="${WORKSPACE_DIR}/${FIRMWARE_NAME}/Debug/${FIRMWARE_NAME}.elf"
        if [ -f "$OUTPUT_ELF" ]; then
            echo "Output: $OUTPUT_ELF"
            cp "$OUTPUT_ELF" "${SOURCE_DIR}/"
            echo "Copied to: ${SOURCE_DIR}/${FIRMWARE_NAME}.elf"
        fi
    else
        echo ""
        echo "ERROR: CLI build failed. Please use Vitis IDE as described above."
        exit 1
    fi
else
    echo "Skipping automatic build. Please follow IDE instructions above."
fi

echo ""
echo "Done!"