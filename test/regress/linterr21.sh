set -xe

which nvc

# A scan of a file with errors: the state machine after them is reported
# only with --recover, together with the damaged lines and a count of the
# machines that were left out
nvc --std=19 --scan --symbols --fsm $TESTDIR/regress/linterr21.sv >plain.json 2>/dev/null || true
if grep -q '"fsms"' plain.json; then exit 1; fi
if grep -q '"damaged"' plain.json; then exit 1; fi

nvc --std=19 --scan --symbols --fsm --recover $TESTDIR/regress/linterr21.sv >rec.json 2>/dev/null || true
grep -q '"fsms":\[' rec.json
grep -q '"damaged":\[\[2,2\],\[20,20\]\]' rec.json
grep -q '"fsms_skipped":1' rec.json
