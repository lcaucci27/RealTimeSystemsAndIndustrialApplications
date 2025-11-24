#!/bin/bash
#
# Deploy Firmware and Applications to Kria KR260
#
# This script copies compiled binaries to the target board via SCP.
#
# Usage: ./deploy.sh [board_ip]
#   board_ip: Optional, defaults to 192.168.1.100
#
# Environment variables:
#   BOARD_IP: Target board IP address
#   BOARD_USER: SSH username (default: root)
#

set -e  # Exit on error

# Configuration
BOARD_IP="${1:-${BOARD_IP:-192.168.1.100}}"
BOARD_USER="${BOARD_USER:-root}"
FIRMWARE_DIR="/lib/firmware"
APP_DIR="/home/root"

# Paths
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RPU_FIRMWARE_DIR="${PROJECT_ROOT}/firmware/rpu"
APU_APP_DIR="${PROJECT_ROOT}/linux/applications"
KERNEL_MODULE_DIR="${PROJECT_ROOT}/linux/kernel-modules"

echo "========================================="
echo "Deploy to Kria KR260"
echo "========================================="
echo "Target:       ${BOARD_USER}@${BOARD_IP}"
echo "Firmware Dir: ${FIRMWARE_DIR}"
echo "App Dir:      ${APP_DIR}"
echo "========================================="
echo ""

# Check connectivity
echo "[1/5] Testing connection to board..."
if ! ping -c 1 -W 2 "$BOARD_IP" > /dev/null 2>&1; then
    echo "ERROR: Cannot reach board at $BOARD_IP"
    echo "Please check:"
    echo "  1. Board is powered on"
    echo "  2. Network cable connected"
    echo "  3. IP address is correct"
    exit 1
fi

if ! ssh -o ConnectTimeout=5 -o BatchMode=yes "${BOARD_USER}@${BOARD_IP}" "echo OK" > /dev/null 2>&1; then
    echo "ERROR: SSH connection failed"
    echo "Please ensure:"
    echo "  1. SSH server is running on board"
    echo "  2. SSH keys are configured (or use password)"
    echo "  3. User '$BOARD_USER' exists"
    exit 1
fi

echo "Connection OK!"
echo ""

# Deploy RPU firmware
echo "[2/5] Deploying RPU firmware..."

RPU_FILES=(
    "performance_test/rpu_perf_test.elf"
    "performance_test/rpu_receiver_io.elf"
    "coherence_test/rpu_coherency_test.elf"
    "coherence_mod_test/rpu_coherency_test_mod.elf"
)

DEPLOYED_COUNT=0
for FILE in "${RPU_FILES[@]}"; do
    FULL_PATH="${RPU_FIRMWARE_DIR}/${FILE}"
    if [ -f "$FULL_PATH" ]; then
        BASENAME=$(basename "$FILE")
        echo "  Copying $BASENAME..."
        scp -q "$FULL_PATH" "${BOARD_USER}@${BOARD_IP}:${FIRMWARE_DIR}/"
        DEPLOYED_COUNT=$((DEPLOYED_COUNT + 1))
    fi
done

if [ $DEPLOYED_COUNT -eq 0 ]; then
    echo "  WARNING: No RPU firmware files found!"
    echo "  Please build firmware first with: scripts/build_rpu.sh"
else
    echo "  Deployed $DEPLOYED_COUNT firmware files"
fi
echo ""

# Deploy APU applications
echo "[3/5] Deploying APU applications..."

APU_FILES=(
    "apu_perf_test"
    "apu_coherency_test"
)

DEPLOYED_COUNT=0
for FILE in "${APU_FILES[@]}"; do
    FULL_PATH="${APU_APP_DIR}/${FILE}"
    if [ -f "$FULL_PATH" ]; then
        echo "  Copying $FILE..."
        scp -q "$FULL_PATH" "${BOARD_USER}@${BOARD_IP}:${APP_DIR}/"
        ssh "${BOARD_USER}@${BOARD_IP}" "chmod +x ${APP_DIR}/${FILE}"
        DEPLOYED_COUNT=$((DEPLOYED_COUNT + 1))
    fi
done

if [ $DEPLOYED_COUNT -eq 0 ]; then
    echo "  WARNING: No APU applications found!"
    echo "  Please build applications first:"
    echo "    cd linux/applications && make"
else
    echo "  Deployed $DEPLOYED_COUNT applications"
fi
echo ""

# Deploy kernel modules
echo "[4/5] Deploying kernel modules..."

KERNEL_FILES=(
    "coherency_stress.ko"
)

DEPLOYED_COUNT=0
for FILE in "${KERNEL_FILES[@]}"; do
    FULL_PATH="${KERNEL_MODULE_DIR}/${FILE}"
    if [ -f "$FULL_PATH" ]; then
        echo "  Copying $FILE..."
        scp -q "$FULL_PATH" "${BOARD_USER}@${BOARD_IP}:${APP_DIR}/"
        DEPLOYED_COUNT=$((DEPLOYED_COUNT + 1))
    fi
done

if [ $DEPLOYED_COUNT -eq 0 ]; then
    echo "  WARNING: No kernel modules found!"
else
    echo "  Deployed $DEPLOYED_COUNT kernel modules"
fi
echo ""

# Deploy setup script
echo "[5/5] Deploying setup script..."
SETUP_SCRIPT="${PROJECT_ROOT}/scripts/setup_experiment.sh"
if [ -f "$SETUP_SCRIPT" ]; then
    scp -q "$SETUP_SCRIPT" "${BOARD_USER}@${BOARD_IP}:${APP_DIR}/"
    ssh "${BOARD_USER}@${BOARD_IP}" "chmod +x ${APP_DIR}/setup_experiment.sh"
    echo "  Setup script deployed"
else
    echo "  WARNING: setup_experiment.sh not found"
fi
echo ""

# Summary
echo "========================================="
echo "Deployment Summary"
echo "========================================="
echo "Files deployed to ${BOARD_USER}@${BOARD_IP}:"
echo ""
echo "Firmware (${FIRMWARE_DIR}):"
ssh "${BOARD_USER}@${BOARD_IP}" "ls -lh ${FIRMWARE_DIR}/*.elf 2>/dev/null | awk '{print \"  \" \$9, \"(\"\$5\")\"}' || echo '  No firmware files'"
echo ""
echo "Applications (${APP_DIR}):"
ssh "${BOARD_USER}@${BOARD_IP}" "ls -lh ${APP_DIR}/apu_* ${APP_DIR}/*.ko ${APP_DIR}/setup_experiment.sh 2>/dev/null | awk '{print \"  \" \$9, \"(\"\$5\")\"}' || echo '  No application files'"
echo ""
echo "========================================="
echo "Next Steps:"
echo "========================================="
echo ""
echo "1. SSH to board:"
echo "   ssh ${BOARD_USER}@${BOARD_IP}"
echo ""
echo "2. Run setup (optional):"
echo "   cd ${APP_DIR}"
echo "   sudo ./setup_experiment.sh"
echo ""
echo "3. Load RPU firmware:"
echo "   echo rpu_perf_test.elf > /sys/class/remoteproc/remoteproc0/firmware"
echo "   echo start > /sys/class/remoteproc/remoteproc0/state"
echo ""
echo "4. Run APU test:"
echo "   sudo ./apu_perf_test 100 results.csv"
echo ""
echo "5. Copy results back to PC:"
echo "   scp ${BOARD_USER}@${BOARD_IP}:${APP_DIR}/results.csv ."
echo ""
echo "========================================="
echo ""
echo "Deployment complete!"