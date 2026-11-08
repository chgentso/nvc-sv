// Self-checking test of `iff` on a three-step transition bin.
// Expected values: IEEE 1800-2017 19.5 (a bin guard that is false at a sampling point keeps the bin from
// being incremented), 19.5.2 (a transition bin is incremented when its sequence completes, i.e. at the
// sample of its last element), 19.11 (one bin: coverage is 0 or 100).
module svcoviffbin2;
  bit [3:0] v; bit en;
  covergroup g;
    cp: coverpoint v {
      bins t = (1 => 2 => 3) iff (en);
    }
  endgroup
  g gi;
  int fails = 0;
  task s(input [3:0] x, input e);
    v = x; en = e; gi.sample();
  endtask
  task run(input e1, e2, e3);
    real got, exp;
    gi = new;
    s(1,e1); s(2,e2); s(3,e3);
    got = gi.get_inst_coverage();
    // the sequence 1=>2=>3 completes at the third sample, whose guard is e3
    exp = e3 ? 100.0 : 0.0;
    if (got < exp - 0.005 || got > exp + 0.005) begin
      fails++;
      $error("en=%0d%0d%0d: coverage %0.2f, expected %0.2f", e1, e2, e3, got, exp);
    end
  endtask
  initial begin
    run(1,1,1); run(0,1,1); run(1,0,1); run(1,1,0); run(0,0,1); run(1,0,0); run(0,1,0); run(0,0,0);
    if (fails == 0) $display("PASSED");
  end
endmodule
