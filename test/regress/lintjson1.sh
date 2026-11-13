set -xe

which nvc

# The probe succeeds only when the option is known
nvc --messages=json --version
! nvc --messages=bogus --version

# An error has a range and related locations
! nvc --std=08 --messages=json -a $TESTDIR/regress/lintjson1.vhd 2>err
test $(wc -l < err) = 1
grep -q '^{"level":"error","message":"S already declared in this region"' err
grep -q '"file":".*lintjson1.vhd","line":6,"col":12,"end_line":6,"end_col":13' err
grep -q '"related":\[{"file":".*lintjson1.vhd","line":5,"col":12,' err
grep -q '"message":"previous declaration was here"' err

# Lint messages carry the rule instead of a suffix
nvc --std=08 --lint --messages=json -a $TESTDIR/lint/unused1.vhd 2>lint
grep -q '"level":"warning"' lint
grep -q '"rule":"unused"' lint
! grep -q 'lint:' lint

# A signal that is alone on its line has a fix that removes the line and
# one declared with another does not
grep -q '"message":"signal .S_UNUSED. is never read".*"fix":{"title":"Remove the unused signal","edits":\[{"file":".*unused1.vhd","line":20,"col":1,"end_line":20,"end_col":[0-9]*,"text":""}\]}' lint
nvc --std=08 --lint --messages=json -a $TESTDIR/regress/lintjson3.vhd 2>multi
grep -q '"message":"signal .A1. is never read"' multi
! grep -q '"fix"' multi

# The list of rules
nvc --lint=list > rules
grep -q '^unused	vhdl,verilog	elaboration	off	' rules
grep -q '^latch	' rules
test $(wc -l < rules) = 4

# Reports from the simulation keep the text form
nvc --std=08 --messages=json -a $TESTDIR/regress/lintjson2.vhd -e lintjson2 -r 2>run
grep -q '^\*\* Note: 0ms+0: hello' run
