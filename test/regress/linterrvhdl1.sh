set -xe

which nvc

# A unit with errors still shows the findings in the constructs that are
# free of errors
! nvc --std=08 --lint --messages=compact -a $TESTDIR/lint/vhderr1.vhd 2>lint
grep -q "vhderr1.vhd:16:.*error: no visible declaration for NOSUCH" lint
grep -q "vhderr1.vhd:8:.*signal B is read in this process.*\[lint:sensitivity\]" lint
grep -q "vhderr1.vhd:11:.*latch inferred for signal L.*\[lint:latch\]" lint
grep -q "vhderr1.vhd:19:.*latch inferred for signal M" lint
grep -q "vhderr1.vhd:22:.*latch inferred for signal N" lint

# A registered concurrent assignment is not a latch
! grep -q "vhderr1.vhd:23:" lint

# Nothing is reported for the processes with errors or for the signals
# of the unit
! grep -q "vhderr1.vhd:1[4-7]:.*warning" lint
! grep -q "vhderr1.vhd:2[4-9]:.*warning" lint
! grep -q "UNUSED_S.*vhderr1" lint
test $(grep -c "lint:partial" lint) = 1
grep -q "vhderr1.vhd:5:.*note: lint is partial for this unit: whole-unit rules and 2 constructs with errors were not checked" lint

# The next unit of the file is checked in full
grep -q "vhderr1.vhd:34:.*lint:sensitivity" lint
grep -q "vhderr1.vhd:32:.*signal 'UNUSED_S' is never read" lint

# The note carries its rule in the JSON form and can be switched off
! nvc --std=08 --lint --messages=json -a $TESTDIR/lint/vhderr1.vhd 2>json
grep -q '"level":"note","message":"lint is partial for this unit.*"rule":"partial"' json
! nvc --std=08 --lint=-partial --messages=compact -a $TESTDIR/lint/vhderr1.vhd 2>nopartial
! grep -q "lint:partial" nopartial
grep -q "lint:sensitivity" nopartial

# Only the part of the unit before a syntax error is trusted
! nvc --std=08 --lint --messages=compact -a $TESTDIR/lint/vhderr2.vhd 2>syn
grep -q "vhderr2.vhd:7:.*lint:sensitivity" syn
test $(grep -c "lint:sensitivity" syn) = 1
! grep -q "lint:latch" syn

# A signal with a bad declaration hides the processes that use it
! nvc --std=08 --lint --messages=compact -a $TESTDIR/lint/vhderr3.vhd 2>decl
grep -q "vhderr3.vhd:13:.*lint:sensitivity" decl
test $(grep -c "lint:sensitivity" decl) = 1

# An error in the entity makes the architecture partial
! nvc --std=08 --lint --messages=compact -a $TESTDIR/lint/vhderr4.vhd 2>ent
grep -q "lint:partial" ent

# A unit without errors has no note and without --lint nothing changes
nvc --std=08 --lint --messages=compact -a $TESTDIR/lint/vhderr5.vhd 2>clean
! grep -q "lint:partial" clean
! nvc --std=08 -a $TESTDIR/lint/vhderr1.vhd 2>plain
! grep -q "lint:" plain
