set -xe

# The simulator is called nvc-sv and reports its own version first
which nvc-sv

nvc-sv --version | head -1 > ver
grep -Eq '^nvc-sv [0-9]+\.[0-9]+\.[0-9]+ \(based on nvc 1\.' ver

# Started under the name of the upstream program the upstream format
# is printed so that tools which parse it keep working
nvc --version | head -1 > alias
grep -Eq '^nvc 1\.[0-9]+.* \(nvc-sv [0-9]+\.[0-9]+\.[0-9]+\)' alias

nvc-sv --help > help
grep -q 'nvc-sv' help
