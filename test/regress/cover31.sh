set -xe

# The Cobertura class for a block is made for each source file and its name
# does not have an architecture for a generate statement
nvc -a $TESTDIR/regress/cover31_b.vhd $TESTDIR/regress/cover31.vhd \
    -e --cover=statement cover31 -r
nvc --cover-export --format=cobertura --relative=$TESTDIR/regress \
    -o export.xml cover31.ncdb

diff -u $TESTDIR/regress/gold/cover31.xml export.xml
