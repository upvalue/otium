#!/bin/bash
# Lint checks for unikernel code quality

CLANG_TIDY=/opt/homebrew/opt/llvm/bin/clang-tidy
FAILED=0

echo "=== Running clang-tidy checks ==="
echo ""

# Find all userspace .cpp files (excluding tests)
USER_FILES=$(find ot/user ot/lib -name "*.cpp" ! -name "*-test.cpp" 2>/dev/null)

if [ -n "$USER_FILES" ]; then
  for file in $USER_FILES; do
    # Run clang-tidy and filter for our specific warnings
    # Ignore compilation errors since we're not passing full flags
    OUTPUT=$($CLANG_TIDY "$file" -- -I. -DOT_ARCH_RISCV -std=c++17 2>&1 | \
      grep -E "warning:.*cppcoreguidelines-avoid-non-const-global-variables" || true)

    if [ -n "$OUTPUT" ]; then
      echo "$OUTPUT"
      FAILED=1
    fi
  done
else
  echo "No user files found"
fi

if [ $FAILED -eq 0 ]; then
  echo "✓ No global variable violations found"
else
  echo ""
  echo "✗ Global variable violations found (see above)"
  echo "  Fix: Refactor to use instance/context structs instead of globals"
fi

echo ""
echo "=== Running semgrep checks ==="
echo ""

# Run semgrep for kernel include checks
if semgrep --config .semgrep/rules/no-kernel-includes-in-userspace.yml . --error --quiet 2>&1; then
  echo "✓ No kernel include violations found"
else
  echo "✗ Kernel include violations found (see above)"
  echo "  Fix: Use 'ot/user/user.hpp' instead of 'ot/core/kernel.hpp'"
  FAILED=1
fi

echo ""
if [ $FAILED -eq 0 ]; then
  echo "✓ All lint checks passed"
  exit 0
else
  echo "✗ Some lint checks failed"
  exit 1
fi
