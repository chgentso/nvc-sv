set -xe

# The same interface from two files does not make its dependents out of date
nvc --std=2019 -a $TESTDIR/regress/svredef1_a.sv
nvc --std=2019 -a $TESTDIR/regress/svredef1.sv
nvc --std=2019 -e svredef1
nvc --std=2019 -r svredef1 | grep "w=1"
