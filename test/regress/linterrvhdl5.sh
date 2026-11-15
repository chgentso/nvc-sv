set -xe

which nvc

# A scan of a VHDL file with errors: the state machine that the errors do
# not touch is reported only with --recover, with the damaged lines and a
# count of the machines that were left out
nvc --std=19 --scan --symbols --fsm $TESTDIR/regress/linterrvhdl5.vhd >plain.json 2>/dev/null || true
if grep -q '"damaged"' plain.json; then exit 1; fi

nvc --std=19 --scan --symbols --fsm --recover $TESTDIR/regress/linterrvhdl5.vhd >rec.json 2>/dev/null || true
grep -q '"fsms":\[' rec.json
grep -q '"damaged":\[\[4,4\],\[25,25\]\]' rec.json
grep -q '"fsms_skipped":1' rec.json
