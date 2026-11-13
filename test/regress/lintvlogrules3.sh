set -xe

which nvc

nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/lintvlogrules3.sv 2>lint || true

# Nothing is reported for the compiler generated objects
! grep -q '\$' lint
! grep -q 'lint:multidriven' lint
