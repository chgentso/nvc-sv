set -xe

# The container of an instance in the symbols of the scan names the
# instance in the netlist once the generate indices are removed

which nvc

nvc --std=08 -a $TESTDIR/netlist/m5v_gen.vhd -e m5v_gtop
nvc --std=08 --netlist m5v_gtop > gen.json
nvc --std=08 --scan --symbols $TESTDIR/netlist/m5v_gen.vhd > scan.json

# Paths of the instances from the scan: TOP + generate labels + label
tr '{' '\n' < scan.json | grep '"kind":"instance"' \
  | sed -e 's/^"name":"\([^"]*\)".*"container":"WORK\.[A-Z0-9_]*-RTL\([^"]*\)".*/M5V_GTOP\2.\1/' \
  | sort > scan.paths

# Paths of the instance modules without indices
grep -o '^"M5V_GTOP\.[^"]*":{' gen.json | sed -e 's/^"//' -e 's/":{$//' \
  -e 's/([0-9]*)//g' | sort -u > elab.paths

cat scan.paths elab.paths

[ $(wc -l < elab.paths) = 4 ]

# Every elaborated instance is a scanned one
[ -z "$(comm -13 scan.paths elab.paths)" ]

# The alternative label replaces the label of the statement
grep -q '^M5V_GTOP\.ALT\.U5$' elab.paths
grep -q '^M5V_GTOP\.A2\.U8$' elab.paths
grep -q '^M5V_GTOP\.ALT\.U5$' scan.paths
