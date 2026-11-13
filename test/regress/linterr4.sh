set -xe

which nvc

# An error in the first file does not stop the second from being checked
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr4a.sv $TESTDIR/regress/linterr4b.sv 2>lint; then exit 1; fi
grep -q "linterr4a.sv:3:.*nosuch" lint
grep -q "linterr4b.sv:2:.*continuous assignment truncates 4 bits to 2 bits \[lint:width\]" lint
! grep -q "linterr4b.sv.*lint:partial" lint
