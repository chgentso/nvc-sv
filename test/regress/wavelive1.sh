set -xe

which nvc
which fstdump

rm -f continue.txt

nvc -a $TESTDIR/regress/wavelive1.vhd -e wavelive1
nvc -r wavelive1 -w --wave-flush=0.05 &
pid=$!

# While the simulation waits, the dump has all the values so far
found=0
for i in $(seq 100); do
    if fstdump wavelive1.fst > mid.dump 2> mid.err && grep -q '^#3000000 wavelive1.count' mid.dump; then
        found=1
        break
    fi
    sleep 0.1
done
test $found = 1

# The values of the first three nanoseconds, and nothing after them
grep '^#3000000 wavelive1.count' mid.dump | grep -q '00000000000000000000000000000011'
! grep -q 'wavelive1.count 00000000000000000000000001100011' mid.dump

touch continue.txt
wait $pid

# The complete dump replaces it when the simulation is done
test ! -e wavelive1.fst.run
test ! -e wavelive1.fst.tmp
fstdump wavelive1.fst > end.dump
grep -q 'wavelive1.count 00000000000000000000000001100011' end.dump
grep '^#3000000 wavelive1.count' end.dump | grep -q '00000000000000000000000000000011'
