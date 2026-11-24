set -xe

which nvc
which fstdump

nvc --std=1993 -a $TESTDIR/regress/vlog_packed1.sv -e vlog_packed1 -r -w --wave=fst_packed1.fst

fstdump fst_packed1.fst > fst_packed1.dump
grep -q "vlog_packed1.v8\[7:0\] 10zx01xz" fst_packed1.dump
grep -q "vlog_packed1.v64\[63:0\] 0000000100100011010001010110zzzzzzzz1001101010111100110111101111" fst_packed1.dump
grep -q "vlog_packed1.v128\[127:0\] 0000000000010001001000100011001101000100010101010110011001111100" fst_packed1.dump
grep -q "vlog_packed1.v256\[255:0\] 10100001101000101010001110100100101001011010011010100111101011111110" fst_packed1.dump
grep -q "vlog_packed1.frc_drv\[15:0\] 0111011110001000" fst_packed1.dump
