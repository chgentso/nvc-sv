set -xe

# --black-box-missing: generics of an instance of a missing entity which
# are of an array type or whose type cannot be inferred do not take the
# place of the generics that follow them, and the type of a generic is
# that of a visible component declaration of the same name

which nvc

nvc --std=08 -a --black-box-missing $TESTDIR/blackbox/gen1pkg.vhd \
    $TESTDIR/blackbox/gen1.vhd -e --black-box-missing gen1 2> boxed.txt
grep -q 'black box for missing unit BRD (library WORK)' boxed.txt
grep -q 'black box for missing unit OTHER (library WORK)' boxed.txt
if grep -q 'does not match' boxed.txt; then exit 1; fi
if grep -q 'already has an actual' boxed.txt; then exit 1; fi
