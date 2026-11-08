set -xe

# Analysing a file again into the same library does not leave its
# compilation unit depending on the previous version of an interface
nvc --std=2019 -a $TESTDIR/regress/svreanal1.sv
nvc --std=2019 -a $TESTDIR/regress/svreanal1.sv
nvc --std=2019 -e svreanal1
nvc --std=2019 -r svreanal1 | grep PASSED
