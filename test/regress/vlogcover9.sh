set -xe

nvc -a $TESTDIR/regress/vlogcover9.sv -e --cover=statement vlogcover9 -r
nvc --cover-export --format=cobertura --relative=$TESTDIR/regress \
    -o export.xml vlogcover9.ncdb
grep -q '<class name="vlogcover9" filename="vlogcover9.sv"' export.xml
