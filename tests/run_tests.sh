#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/attendance"
PASS=0
FAIL=0

run_case() {
    local name="$1"
    local input="$2"
    local expect="$3"

    local output
    output="$(printf '%s\n' "$input" | "$BIN" --model utxo --difficulty 1 2>&1 || true)"

    if echo "$output" | grep -q "$expect"; then
        echo "PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name (expected pattern: $expect)"
        FAIL=$((FAIL + 1))
    fi
}

cd "$ROOT"
make clean >/dev/null
make >/dev/null

run_case "mark present queues pending" \
    $'mark ALU001 PRESENT\npending\nquit\n' \
    "PENDING"

run_case "absent creates no transaction" \
    $'mark ALU002 ABSENT\npending\nquit\n' \
    "No transaction created"

run_case "solo mining confirms block" \
    $'mark ALU001 LATE\nmine solo\nview\nquit\n' \
    "Confirmed block"

run_case "insufficient balance transfer rejected" \
    $'transfer ALU001 ALU002 100\nquit\n' \
    "Insufficient balance"

run_case "cloud unprofitable warning" \
    $'mark ALU003 PRESENT\nmine cloud 3 500 500\nquit\n' \
    "unprofitable"

run_case_account() {
    local name="$1"
    local input="$2"
    local expect="$3"
    local output

    output="$(printf '%s\n' "$input" | "$BIN" --model account --difficulty 1 2>&1 || true)"
    if echo "$output" | grep -q "$expect"; then
        echo "PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name (expected pattern: $expect)"
        FAIL=$((FAIL + 1))
    fi
}

run_case_account "failed nonce rejected" \
    $'mark ALU001 PRESENT\nmine solo\ntransfer_nonce ALU001 ALU002 1 99\nquit\n' \
    "Invalid nonce"

rm -f attendance_chain.dat attendance_private.pem attendance_public.pem

echo ""
echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
