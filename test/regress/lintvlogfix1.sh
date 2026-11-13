set -xe

which nvc

# A net or variable that is alone on its line and is not assigned has a fix
# that removes the line
nvc --std=19 --lint --messages=json -a $TESTDIR/regress/lintvlogfix1.sv -e lintvlogfix1 2>lint
grep -q '"message":"net .w_dead. is never read".*"fix":{"title":"Remove the unused net","edits":\[{"file":".*lintvlogfix1.sv","line":3,"col":1,"end_line":3,"end_col":[0-9]*,"text":""}\]}' lint
grep -q '"message":"variable .r_dead. is never read".*"fix":{"title":"Remove the unused variable","edits":\[{"file":".*lintvlogfix1.sv","line":4,' lint

# Not when it shares the declaration, is initialised or is assigned
for n in w_a w_b r_written r_init; do
  ! grep "never read" lint | grep "'$n'" | grep -q '"fix"'
done
grep -q "'w_a' is never read" lint
grep -q "'r_written' is never read" lint
