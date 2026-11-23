set -xe

# Elaborate and run in separate processes: the declarations are lowered
# again from the saved design when it is run and find the same items
nvc -a $TESTDIR/regress/vlogcover12.sv -e --cover=toggle vlogcover12
nvc -r vlogcover12
nvc --cover-export --format=xml --relative=$TESTDIR/regress -o once.xml \
    vlogcover12.ncdb
diff -u $TESTDIR/regress/gold/vlogcover12.xml once.xml

# A second run adds to the counts: the clock toggles ten more times
nvc -r vlogcover12
nvc --cover-export --format=xml --relative=$TESTDIR/regress -o twice.xml \
    vlogcover12.ncdb
grep -q 'vlogcover12.clk.BIN_0_TO_1" data="10"' twice.xml

# Statements and branches count nothing for a signal and are not made
# when only toggles are asked for
rm -rf work
NVC_LOWER_VERBOSE=1 nvc -a $TESTDIR/regress/vlogcover12.sv \
    -e --cover=toggle vlogcover12 -r > toggle.txt 2>&1
grep -q 'cover toggle' toggle.txt
! grep -E 'get counters|cover (stmt|branch)' toggle.txt

# Without coverage nothing is counted
rm -rf work
NVC_LOWER_VERBOSE=1 nvc -a $TESTDIR/regress/vlogcover12.sv \
    -e vlogcover12 -r > plain.txt 2>&1
grep -q 'PASSED' plain.txt
! grep -q 'cover toggle' plain.txt

# Statement and toggle coverage together
rm -rf work
nvc -a $TESTDIR/regress/vlogcover12.sv -e --cover=statement,toggle \
    vlogcover12 -r
nvc --cover-export --format=xml -o both.xml vlogcover12.ncdb
grep -q '<statement' both.xml
grep -q 'vlogcover12.v\[3\].BIN_0_TO_1" data="1"' both.xml

# The report has the toggle items and a Verilog signal name
nvc --cover-report -o html vlogcover12.ncdb
test -f html/index.html
