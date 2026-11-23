set -e

cat > crv.sv <<'EOS'
class C;
  rand bit [65:0] q[4];
  rand int n;
  constraint c { n inside {[0:3]}; q[n] == 1; }
endclass
module m;
  task automatic t; C c = new; void'(c.randomize()); endtask
  initial begin #10; t(); end
endmodule
EOS

nvc --std=2019 -a crv.sv

# Not reported before the task runs
nvc -e m
nvc -e --check-crv m > out.txt 2>&1 && exit 1
grep "sorry, an element of an array of values wider than 64 bits" out.txt

# A design where everything is supported elaborates
cat > ok.sv <<'EOS'
class D;
  rand int x;
  constraint c { x inside {[1:5]}; }
endclass
module ok;
  D d = new;
  initial begin
    if (!d.randomize()) $fatal(1, "failed");
    $display("PASSED");
  end
endmodule
EOS
nvc --std=2019 -a ok.sv -e --check-crv ok -r | grep PASSED
