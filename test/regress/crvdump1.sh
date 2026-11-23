set -xe

nvc --std=2019 -a $TESTDIR/regress/crvdump1.sv -e crvdump1

nvc -r crvdump1 > base.txt
test $(grep -c '^[0-9]' base.txt) -eq 3

# Dumping the problems for an SMT solver does not change the values
NVC_CRV_DUMP_SMT2=$PWD/dump nvc -r crvdump1 > dump.txt
cmp base.txt dump.txt

# One problem and one check of the circuit per call, as bit-vector terms
test -f dump.0.smt2
test -f dump.2.smt2
test -f dump.0.miter.smt2
grep -q '^(set-logic QF_BV)' dump.0.smt2
grep -q '^(declare-const v0 (_ BitVec 8))' dump.0.smt2
grep -q 'bvmul' dump.0.smt2
grep -q 'bvurem' dump.0.smt2
grep -q '^(assert (distinct' dump.0.smt2
grep -q '^(check-sat)' dump.0.smt2
grep -q '^(check-sat)' dump.0.miter.smt2
grep -q 'define-fun g[0-9]* () Bool (and' dump.0.miter.smt2
exit 0
