set -e  # Bail out if anything fails

# Basic config, can override with args
BOARD_IP="${1:-${BOARD_IP:-192.168.1.100}}"
BOARD_USER="${BOARD_USER:-root}"
ITERATIONS="${2:-100}"
OUTPUT_FILE="performance_results.csv"
FIRMWARE_NAME="rpu_perf_test.elf"

# Figure out where everything lives
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RESULTS_DIR="${PROJECT_ROOT}/results"

echo "========================================="
echo "Performance Test Execution"
echo "========================================="
echo "Target:      ${BOARD_USER}@${BOARD_IP}"
echo "Firmware:    ${FIRMWARE_NAME}"
echo "Iterations:  ${ITERATIONS} per packet size"
echo "Output:      ${OUTPUT_FILE}"
echo "========================================="
echo ""

# Make sure we can actually reach the board
echo "[1/6] Checking connection..."
if ! ssh -o ConnectTimeout=5 "${BOARD_USER}@${BOARD_IP}" "echo OK" > /dev/null 2>&1; then
    echo "ERROR: Cannot connect to board"
    echo "Please check network and SSH configuration"
    exit 1
fi
echo "Connected!"
echo ""

# Check if we already deployed the binaries
echo "[2/6] Verifying deployment..."
CHECK_CMD="test -f /lib/firmware/${FIRMWARE_NAME} && test -f /home/root/apu_perf_test && echo OK || echo MISSING"
if ! ssh "${BOARD_USER}@${BOARD_IP}" "$CHECK_CMD" | grep -q "OK"; then
    echo "ERROR: Required files not found on board"
    echo "Please run: ./scripts/deploy.sh"
    exit 1
fi
echo "Files verified!"
echo ""

# Stop RPU if it's already running something else
echo "[3/6] Preparing RPU..."
ssh "${BOARD_USER}@${BOARD_IP}" << 'EOSSH'
# Check if RPU is currently running
if [ -d /sys/class/remoteproc/remoteproc0 ]; then
    CURRENT_STATE=$(cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null || echo "unknown")
    if [ "$CURRENT_STATE" = "running" ]; then
        echo "  Stopping current RPU firmware..."
        echo stop > /sys/class/remoteproc/remoteproc0/state
        sleep 1
    fi
else
    echo "ERROR: Remoteproc not available"
    echo "Please ensure:"
    echo "  1. Kernel module 'zynqmp_r5_remoteproc' is loaded"
    echo "  2. Device tree is properly configured"
    exit 1
fi
EOSSH

if [ $? -ne 0 ]; then
    echo "ERROR: Failed to prepare RPU"
    exit 1
fi
echo "RPU ready!"
echo ""

# Load our firmware and start the RPU
echo "[4/6] Loading RPU firmware..."
ssh "${BOARD_USER}@${BOARD_IP}" << EOSSH
# Tell remoteproc which firmware to use
echo "${FIRMWARE_NAME}" > /sys/class/remoteproc/remoteproc0/firmware

# Fire it up
echo start > /sys/class/remoteproc/remoteproc0/state

# Give it a moment to initialize
sleep 2
STATE=\$(cat /sys/class/remoteproc/remoteproc0/state)
if [ "\$STATE" != "running" ]; then
    echo "ERROR: RPU failed to start (state: \$STATE)"
    echo "Check dmesg for errors:"
    dmesg | tail -20 | grep -i remoteproc
    exit 1
fi

echo "  RPU state: \$STATE"
echo "  RPU firmware loaded and running!"
EOSSH

if [ $? -ne 0 ]; then
    echo "ERROR: Failed to start RPU"
    echo "Possible causes:"
    echo "  1. Firmware binary incompatible with platform"
    echo "  2. Memory regions not available"
    echo "  3. Permission issues"
    exit 1
fi
echo ""

# Now run the actual test from APU side
echo "[5/6] Running APU performance test..."
echo "This will take several minutes (14 packet sizes × ${ITERATIONS} iterations)..."
echo ""

ssh "${BOARD_USER}@${BOARD_IP}" << EOSSH
cd /home/root

# Execute the test
sudo ./apu_perf_test ${ITERATIONS} ${OUTPUT_FILE}

# Make sure we got results
if [ ! -f ${OUTPUT_FILE} ]; then
    echo "ERROR: Results file not generated"
    exit 1
fi

# Quick summary
LINES=\$(wc -l < ${OUTPUT_FILE})
echo ""
echo "Test complete!"
echo "Results: ${OUTPUT_FILE} (\$LINES lines)"
EOSSH

if [ $? -ne 0 ]; then
    echo "ERROR: Test execution failed"
    echo "Check RPU console output for errors"
    exit 1
fi
echo ""

# Pull results back to our machine
echo "[6/6] Retrieving results..."
mkdir -p "$RESULTS_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOCAL_FILE="${RESULTS_DIR}/${OUTPUT_FILE%.csv}_${TIMESTAMP}.csv"

scp "${BOARD_USER}@${BOARD_IP}:/home/root/${OUTPUT_FILE}" "$LOCAL_FILE"

if [ -f "$LOCAL_FILE" ]; then
    LINES=$(wc -l < "$LOCAL_FILE")
    SIZE=$(du -h "$LOCAL_FILE" | cut -f1)
    echo "Results saved to: $LOCAL_FILE"
    echo "  Lines: $LINES"
    echo "  Size:  $SIZE"
    
    # Keep a "latest" version for convenience
    cp "$LOCAL_FILE" "${RESULTS_DIR}/${OUTPUT_FILE}"
    echo "  Copied to: ${RESULTS_DIR}/${OUTPUT_FILE}"
else
    echo "ERROR: Failed to retrieve results"
    exit 1
fi
echo ""

# Clean shutdown of RPU
echo "Stopping RPU..."
ssh "${BOARD_USER}@${BOARD_IP}" "echo stop > /sys/class/remoteproc/remoteproc0/state" || true
echo ""

# All done, tell the user what to do next
echo "========================================="
echo "Test Execution Complete!"
echo "========================================="
echo ""
echo "Results file: $LOCAL_FILE"
echo ""
echo "Next steps:"
echo "  1. Analyze results:"
echo "     cd ${PROJECT_ROOT}/analysis"
echo "     python3 analyze_performance.py ${LOCAL_FILE} output/"
echo ""
echo "  2. View plots in: output/"
echo ""
echo "  3. Re-run test with different iterations:"
echo "     ./scripts/run_tests.sh ${BOARD_IP} 200"
echo ""
echo "========================================="