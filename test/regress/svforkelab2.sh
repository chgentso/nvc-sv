set -xe

pwd
which nvc

# IEEE 1800-2017 13.4.4: a function may contain fork-join_none only when
# the calling thread was created by an initial or always procedure, and
# otherwise "implementations shall issue an error either at compile time
# or run time".  A constructor called from a static variable initialiser
# is such a case.

cat >top.sv <<EOT
module top;
  class W;
    function new();
      fork
        #1 \$display("child");
      join_none
    endfunction
  endclass
  W w = new;
  initial #5 \$display("end");
endmodule
EOT

nvc -a top.sv
! nvc -e top -r 2>&1 | tee out
grep "fork-join_none in a function is only allowed" out
! grep child out
echo PASSED
