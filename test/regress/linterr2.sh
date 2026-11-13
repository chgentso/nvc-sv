set -xe

which nvc

# A block with an error is not checked, another block is
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr2.sv 2>lint; then exit 1; fi
grep -q "linterr2.sv:5:.*undefined_name" lint
grep -q "linterr2.sv:9:.*assignment truncates 4 bits to 2 bits \[lint:width\]" lint
grep -q "linterr2.sv:7:.*latch inferred for 'q'" lint || grep -q "linterr2.sv:7:.*latch" lint
! grep -q "linterr2.sv:2:.*latch" lint
grep -q 'lint:partial' lint
