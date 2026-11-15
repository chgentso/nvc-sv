set -xe

which nvc

# After a syntax error: a name that is not declared is reported once, a
# package declared earlier in the file is not missing, a stray closing
# bracket is dropped and the declarations after it still count, and an
# end inside an expression does not close the block
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr18.sv 2>lint; then exit 1; fi
test "$(grep -c "no visible declaration for 'foo'" lint)" = 1
if grep -q 'no package named' lint; then exit 1; fi
test "$(grep -c 'unexpected )' lint)" = 1
test "$(grep -c 'unexpected end' lint)" = 1
if grep -q 'while parsing module or generate item' lint; then exit 1; fi
