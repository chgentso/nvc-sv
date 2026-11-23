set -xe

# Exclude files on a design with Verilog names

nvc -a $TESTDIR/regress/vlogcover17.sv
nvc -e --cover=statement,branch,toggle -gN=2 vlogcover17 -r
mv vlogcover17.ncdb union.ncdb

# Names in an exclude file need not have the case of the Verilog source
cat > ef.txt <<'EOT'
exclude WORK.VLOGCOVER17.G[0].Q[1].*
exclude WORK.vlogcover17.Mode[*].*
EOT
nvc --cover-report --exclude-file ef.txt -o excl union.ncdb 2>&1 | tee excl.txt
grep -q "excluding toggle: 'WORK.vlogcover17.g\[0\].q\[1\].BIN_1_TO_0'" excl.txt
grep -q "excluding toggle: 'WORK.vlogcover17.mode\[1\].BIN_0_TO_1'" excl.txt
! grep -q "does not match any coverage item" excl.txt

# A pattern that matches nothing is still reported
echo "exclude WORK.vlogcover17.nothing.*" > ef2.txt
nvc --cover-report --exclude-file ef2.txt -o excl2 union.ncdb 2>&1 | tee excl2.txt
grep -q "does not match any coverage item" excl2.txt

# The fold command finds the scopes of the generate blocks in any case
echo "fold WORK.vlogcover17.G[0] WORK.vlogcover17.G[1]" > ef3.txt
nvc --cover-report --exclude-file ef3.txt -o fold union.ncdb 2>&1 | tee fold.txt
grep -q "Source - WORK.vlogcover17.g\[1\]" fold.txt
! grep -q "does not match any coverage scope" fold.txt
