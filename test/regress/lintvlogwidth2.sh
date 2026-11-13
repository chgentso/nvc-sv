set -xe

which nvc

nvc --std=19 --lint --messages=json -a $TESTDIR/regress/lintvlogwidth2.sv 2>lint || true
nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/lintvlogwidth2.sv 2>compact || true

# Parameters with an explicit type
grep -q "lintvlogwidth2.sv:3:.*parameter value truncates 8 bits to 4 bits \[lint:width\]" compact
grep -q "lintvlogwidth2.sv:4:.*parameter value truncates 33 bits to 32 bits \[lint:width\]" compact
# An untyped parameter takes the width of its value
! grep -q "lintvlogwidth2.sv:5:" compact

# Initial values of variables
grep -q "lintvlogwidth2.sv:6:.*initial value truncates 2 bits to 1 bits \[lint:width\]" compact
grep -q "lintvlogwidth2.sv:7:.*initial value truncates 8 bits to 4 bits \[lint:width\]" compact
! grep -q "lintvlogwidth2.sv:[89]:" compact

# The net declaration assignment
grep -q "lintvlogwidth2.sv:10:.*continuous assignment truncates 2 bits to 1 bits" compact

# The fix selects the low bits
grep '"message":"parameter value truncates 8 bits to 4 bits' lint | grep -q '"fix":{"title":"Select the low bits","edits":\[{"file":".*","line":3,.*"text":"Q\[3:0\]"'
grep '"message":"initial value truncates 8 bits to 4 bits' lint | grep -q '"text":"in\[3:0\]"'

# The port connection check still reports
nvc --std=19 --lint=-width -a $TESTDIR/regress/lintvlogwidth2.sv 2>quiet || true
! grep -q 'lint:' quiet
