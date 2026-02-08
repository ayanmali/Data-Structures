#!/bin/bash
pass=0
fail=0
total=100

for i in $(seq 1 $total); do
    if ./spmc_test 2>&1 | grep -q "10/10 tests passed"; then
        pass=$((pass + 1))
        echo -n "."
    else
        fail=$((fail + 1))
        echo -n "X"
    fi
done

echo ""
echo "Results: $pass/$total passed, $fail/$total failed"

if [ $fail -eq 0 ]; then
    echo "SUCCESS: All tests passed!"
    exit 0
else
    echo "FAILURE: Some tests failed"
    exit 1
fi
