#!/bin/sh
set -eu
log=$(mktemp)
out=$(mktemp)
trap 'rm -f "$log" "$out" /tmp/cansat-dashboard' EXIT
g++ -std=c++17 -Wall -Wextra -pedantic ground/dashboard.cpp -o /tmp/cansat-dashboard
printf '100,2,2,2145,100123,0,1,0,3\n100,2,2,2145,100123,0,0,335,335,3,0,3\n' | /tmp/cansat-dashboard "$log" > "$out"
diff -u tests/dashboard_expected.csv "$log"
