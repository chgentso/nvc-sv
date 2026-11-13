set -xe

which nvc

nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/lintvlogrules2.sv -e lintvlogrules2 2>lint || true

grep -q "lintvlogrules2.sv:8:.*input port 'b' of 'lintvlogrules2_sub' is left unconnected \[lint:unconnected\]" lint
test $(grep -c 'lint:unconnected' lint) = 1

grep -q "lintvlogrules2.sv:12:.*variable 'r' is assigned by more than one always block \[lint:multidriven\]" lint
test $(grep -c 'lint:multidriven' lint) = 1

nvc --lint=list | grep -q '^unconnected	verilog	elaboration	off	'
nvc --lint=list | grep -q '^multidriven	verilog	analysis	off	'
