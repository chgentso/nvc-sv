set -xe

# Elaborate and run in separate processes: the processes are lowered
# again from the saved design when it is run
nvc -a $TESTDIR/regress/vlogcover2.sv -e --cover=statement vlogcover2
[ -f vlogcover2.ncdb ]
nvc -r vlogcover2
nvc --cover-export --format=xml --relative=$TESTDIR/regress -o once.xml \
    vlogcover2.ncdb
diff -u $TESTDIR/regress/gold/vlogcover2.xml once.xml

# Running again adds to the counts in the database
nvc -r vlogcover2
nvc --cover-export --format=xml --relative=$TESTDIR/regress -o twice.xml \
    vlogcover2.ncdb
diff -u $TESTDIR/regress/gold/vlogcover5.xml twice.xml

# With no coverage the design has no counters and no counting code
rm -rf work
NVC_LOWER_VERBOSE=1 nvc -a $TESTDIR/regress/vlogcover2.sv -e vlogcover2 -r \
    > plain.txt 2>&1
grep -q 'PASSED' plain.txt
! grep -E 'get counters|cover (stmt|branch)' plain.txt

# The same with only toggle coverage which uses neither
rm -rf work
NVC_LOWER_VERBOSE=1 nvc -a $TESTDIR/regress/vlogcover2.sv -e --cover=toggle \
    vlogcover2 -r > toggle.txt 2>&1
grep -q 'PASSED' toggle.txt
! grep -E 'get counters|cover (stmt|branch)' toggle.txt

# Statement coverage does count
rm -rf work
NVC_LOWER_VERBOSE=1 nvc -a $TESTDIR/regress/vlogcover2.sv \
    -e --cover=statement vlogcover2 -r > stmt.txt 2>&1
grep -q 'get counters' stmt.txt
grep -q 'cover stmt' stmt.txt
! grep -q 'cover branch' stmt.txt

# The same for branches, found again when the design is run
rm -rf work
nvc -a $TESTDIR/regress/vlogcover7.sv -e --cover=branch vlogcover7
nvc -r vlogcover7
nvc --cover-export --format=xml --relative=$TESTDIR/regress -o branch.xml \
    vlogcover7.ncdb
diff -u $TESTDIR/regress/gold/vlogcover7.xml branch.xml
