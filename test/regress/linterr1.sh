set -xe

which nvc

# An error does not hide the lint warnings of the rest of the file
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr1.sv 2>lint; then exit 1; fi
grep -q "linterr1.sv:6:.*'w' cannot be assigned in a procedural block" lint
grep -q "linterr1.sv:3:.*initial value truncates 2 bits to 1 bits \[lint:width\]" lint
grep -q "linterr1.sv:4:.*parameter value truncates 8 bits to 4 bits \[lint:width\]" lint
grep -q "linterr1.sv:7:.*assignment truncates 4 bits to 2 bits \[lint:width\]" lint
grep -q "linterr1.sv:9:.*case statement in combinational code has no default \[lint:case-default\]" lint
# The module after the error is checked too
grep -q "linterr1.sv:16:.*continuous assignment truncates 4 bits to 2 bits \[lint:width\]" lint
# One note that the lint was partial, for the module with the error
test $(grep -c 'lint:partial' lint) = 1
grep -q "linterr1.sv:1:.*lint is partial for this unit" lint

# The note can be switched off, the rest stays
if nvc --std=19 --lint=-partial --messages=compact -a $TESTDIR/regress/linterr1.sv 2>quiet; then exit 1; fi
! grep -q 'lint:partial' quiet
grep -q 'lint:width' quiet

# Without --lint only the error is reported
if nvc --std=19 -a $TESTDIR/regress/linterr1.sv 2>plain; then exit 1; fi
! grep -q 'lint:' plain
grep -q "cannot be assigned" plain
