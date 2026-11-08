set -xe

# The random sequence is the same on every run unless a seed is given
nvc --std=2019 -a $TESTDIR/regress/svrandseed1.sv -e svrandseed1
nvc -r svrandseed1 > run1.txt
nvc -r svrandseed1 > run2.txt
cmp run1.txt run2.txt

nvc --seed=5 -r svrandseed1 > seed5.txt
nvc --seed=5 -r svrandseed1 > seed5b.txt
nvc --seed=6 -r svrandseed1 > seed6.txt
cmp seed5.txt seed5b.txt
cmp seed5.txt seed6.txt && exit 1
cmp seed5.txt run1.txt && exit 1
exit 0
