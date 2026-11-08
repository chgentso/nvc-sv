// Expected values: IEEE 1800-2017 6.11 (time is a 64-bit unsigned 4-state type; int is signed),
// 20.3 ($time is 0 at time 0), 21.2.1 (%d, %h)
module svtimeunsigned1;
  time t; bit [63:0] u; int i;
  int errs;
  string st;
  initial begin
    t = -1; u = -1; i = -1;
    st = $sformatf("%0d %0d %0d", t, u, i);
    if (st != "18446744073709551615 18446744073709551615 -1") begin $error("line1 '%s'", st); errs++; end
    if ((t > 0) !== 1'b1 || (u > 0) !== 1'b1) begin $error("unsigned compare"); errs++; end
    if (($time > 0) !== 1'b0) begin $error("time>0 at 0"); errs++; end
    st = $sformatf("%0t %0d", $time, $time);
    if (st != "0 0") begin $error("time text '%s'", st); errs++; end
    t = 64'hffff_ffff_ffff_fff0;
    st = $sformatf("%0d %h", t, t);
    if (st != "18446744073709551600 fffffffffffffff0") begin $error("line5 '%s'", st); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
