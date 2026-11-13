set -xe

which nvc

nvc --std=19 --lint --messages=json -a $TESTDIR/regress/lintvlogfix2.sv -e lintvlogfix2 2>lint

# A vector named on its own is truncated by selecting its low bits
grep -q '"line":12,.*"fix":{"title":"Select the low bits","edits":\[{"file":".*lintvlogfix2.sv","line":12,"col":10,"end_line":12,"end_col":12,"text":"r8\[3:0\]"}\]}' lint
grep -q '"line":15,.*"text":"r8\[3:0\]"' lint
grep -q 'connection to input port .a. truncates.*"text":"r8\[3:0\]"' lint

# An expression has no fix
grep '"line":13,' lint | grep -q 'truncates 8 bits'
! grep '"line":13,' lint | grep -q '"fix"'
