#!/bin/bash
# Runs inside the guest. Sequential battery with RC capture; single sentinel.
echo PLAN_AUDIT_BEGIN
cat /proc/cmdline
echo "--- syscalltlb ---"
syscalltlb 2000
echo SYSCALLTLB_RC=$?
echo "--- forktest ---"
forktest
echo FORKTEST_RC=$?
echo "--- clonetest ---"
clonetest
echo CLONETEST_RC=$?
echo "--- cowtest ---"
cowtest
echo COWTEST_RC=$?
echo PLAN_AUDIT_END
