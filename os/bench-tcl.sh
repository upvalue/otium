#!/bin/bash
# bench-tcl.sh - Benchmark Tcl interpreter optimizations
#
# Compiles tcl-repl with different optimization flags and measures
# performance on bench-mandelbrot.tcl

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$SCRIPT_DIR/bench-mandelbrot.tcl"
RUNS=3
BUILD_DIR="$SCRIPT_DIR/build-bench"

# Optimization flag configurations to test
declare -a CONFIG_NAMES=(
  "baseline"
  "command-hash"
)

declare -a CONFIG_FLAGS=(
  ""
  "-DTCL_OPT_COMMAND_HASH"
)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "======================================"
echo "Tcl Interpreter Optimization Benchmark"
echo "======================================"
echo ""
echo "Script: $SCRIPT"
echo "Runs per config: $RUNS"
echo ""

# Store results for comparison
declare -a RESULTS

for i in "${!CONFIG_NAMES[@]}"; do
  name="${CONFIG_NAMES[$i]}"
  flags="${CONFIG_FLAGS[$i]}"

  echo -e "${YELLOW}[$name]${NC} Compiling with flags: ${flags:-'(none)'}"

  # Clean and setup build directory
  rm -rf "$BUILD_DIR"

  # Setup meson with custom cpp_args
  if [ -z "$flags" ]; then
    meson setup "$BUILD_DIR" --buildtype=release > /dev/null 2>&1
  else
    meson setup "$BUILD_DIR" --buildtype=release -Dcpp_args="$flags" > /dev/null 2>&1
  fi

  # Compile
  meson compile -C "$BUILD_DIR" tcl-repl > /dev/null 2>&1

  # Run benchmark multiple times and collect times
  total_time=0
  echo -n "  Running: "

  for run in $(seq 1 $RUNS); do
    # Use 'time' to measure wall clock time
    start_time=$(python3 -c 'import time; print(time.time())')
    "$BUILD_DIR/tcl-repl" "$SCRIPT" > /dev/null 2>&1
    end_time=$(python3 -c 'import time; print(time.time())')

    elapsed=$(python3 -c "print(${end_time} - ${start_time})")
    total_time=$(python3 -c "print(${total_time} + ${elapsed})")
    echo -n "."
  done
  echo ""

  # Calculate average
  avg_time=$(python3 -c "print(${total_time} / ${RUNS})")
  RESULTS+=("$avg_time")

  echo -e "  Average time: ${GREEN}${avg_time}s${NC}"
  echo ""
done

# Print summary table
echo "======================================"
echo "Summary"
echo "======================================"
printf "%-20s %12s %10s\n" "Configuration" "Time (s)" "Speedup"
printf "%-20s %12s %10s\n" "-------------" "--------" "-------"

baseline="${RESULTS[0]}"
for i in "${!CONFIG_NAMES[@]}"; do
  name="${CONFIG_NAMES[$i]}"
  time="${RESULTS[$i]}"
  speedup=$(python3 -c "print(f'{${baseline} / ${time}:.2f}x')")
  printf "%-20s %12.3f %10s\n" "$name" "$time" "$speedup"
done

# Cleanup
rm -rf "$BUILD_DIR"

echo ""
echo "Done!"
