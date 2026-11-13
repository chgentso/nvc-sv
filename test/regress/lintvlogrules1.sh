set -xe

which nvc

nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/lintvlogrules1.sv 2>lint || true

# A net that is declared by its use
grep -q "lintvlogrules1.sv:5:.*net 'implicit_w' is declared implicitly by its use \[lint:implicit-net\]" lint
! grep -q "other_w" lint

# A variable assigned in both ways
grep -q "lintvlogrules1.sv:12:.*'b' is assigned with both blocking and non-blocking assignments \[lint:blocking\]" lint
! grep -q "'a' is assigned" lint
! grep -q "'e' is assigned" lint

# A case without default only in combinational code
test $(grep -c 'lint:case-default' lint) = 1
grep -q "lintvlogrules1.sv:17:.*case statement in combinational code has no default \[lint:case-default\]" lint

# The rules are listed and can be switched off
nvc --lint=list | grep -q '^implicit-net	verilog	analysis	off	'
nvc --lint=list | grep -q '^blocking	verilog	'
nvc --lint=list | grep -q '^case-default	verilog	'
nvc --std=19 --lint=-implicit-net,-blocking,-case-default,-multidriven --messages=compact -a $TESTDIR/regress/lintvlogrules1.sv 2>quiet || true
! grep -q 'lint:' quiet
