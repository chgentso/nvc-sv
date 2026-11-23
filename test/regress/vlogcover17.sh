set -xe

nvc -a $TESTDIR/regress/vlogcover17.sv

# Two runs with different numbers of generate blocks and clock cycles
nvc -e --cover=statement,branch,toggle -gN=1 -gCYCLES=2 vlogcover17 -r
mv vlogcover17.ncdb a.ncdb
nvc -e --cover=statement,branch,toggle -gN=3 -gCYCLES=3 vlogcover17 -r
mv vlogcover17.ncdb b.ncdb

# Union has the blocks of both runs and adds the counts
nvc --cover-merge -o union.ncdb a.ncdb b.ncdb
nvc --cover-export --format=xml -o union.xml union.ncdb
grep -q 'name="g\[2\]"' union.xml
grep -q 'vlogcover17.clk.BIN_0_TO_1" data="5"' union.xml

# Intersection has only the blocks in both
nvc --cover-merge --merge-mode=intersect -o inter.ncdb a.ncdb b.ncdb
nvc --cover-export --format=xml -o inter.xml inter.ncdb
grep -q 'name="g\[0\]"' inter.xml
! grep -q 'name="g\[1\]"' inter.xml
grep -q 'vlogcover17.clk.BIN_0_TO_1" data="5"' inter.xml

# The report has one page for each file and shows the bits of a vector
nvc --cover-report -o html union.ncdb
test -f html/index.html
nvc --cover-report --per-file -o perfile union.ncdb
test -f perfile/hier/vlogcover17.sv.html
grep -q '<td>\[1\]</td>' perfile/hier/vlogcover17.sv.html
grep -q 'GetExclude(.exclude WORK.vlogcover17.g\[0\].q\[1\].BIN_1_TO_0' \
     perfile/hier/vlogcover17.sv.html

# The coverage specification with an excluded instance of a shared module
# when the design is run in another process
rm -rf work
nvc -a $TESTDIR/regress/vlogcover16.sv
nvc -e --cover=statement,branch,toggle \
    --cover-spec=$TESTDIR/regress/data/vlogcover16.spec vlogcover16
nvc -r vlogcover16
nvc --cover-export --format=xml --relative=$TESTDIR/regress -o spec.xml \
    vlogcover16.ncdb
diff -u $TESTDIR/regress/gold/vlogcover16.xml spec.xml
