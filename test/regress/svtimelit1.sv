// Time literals with a fractional part, see IEEE 1800-2017 section 5.8
// A delay, given as a time literal or not, is rounded to the time precision
// of the module (5.8, 3.14.2.1)

`timescale 1ns/1ps
module tl_ps;
  time tv;
  real r;
  parameter time p = 10.0ns;
  localparam real lr = 0.1ps;
  real t0;

  task check(input string what, input real got, input real exp);
    if (got > exp + 1e-9 || got < exp - 1e-9)
      $fatal(1, "%s: got %f expected %f", what, got, exp);
  endtask

  initial begin
    check("p", p, 10);
    check("lr", lr, 0.0001);
    r = 1.5ns;
    check("r", r, 1.5);
    r = 2 * 1.5ns;
    check("2*r", r, 3.0);
    r = 1.5s;
    check("1.5s", r, 1.5e9);
    r = 10.0ns;
    check("10.0ns", r, 10);
    tv = 1.5ns;
    check("tv", tv, 2);   // Rounded when converted to an integer
    tv = 2.4ns;
    check("tv2", tv, 2);

    t0 = $realtime; #1.5ns;
    check("#1.5ns", $realtime - t0, 1.5);
    t0 = $realtime; #(7.9ns);
    check("#(7.9ns)", $realtime - t0, 7.9);
    t0 = $realtime; #(2 * 1.5ns);
    check("#(2*1.5ns)", $realtime - t0, 3.0);
    t0 = $realtime; #0.1ps;
    check("#0.1ps", $realtime - t0, 0);
    t0 = $realtime; #1.4999ns;
    check("#1.4999ns", $realtime - t0, 1.5);
    t0 = $realtime; #0.0016ns;
    check("#0.0016ns", $realtime - t0, 0.002);
    t0 = $realtime; #1.5;
    check("#1.5", $realtime - t0, 1.5);
    t0 = $realtime; #1_0.2_5us;
    check("#10.25us", $realtime - t0, 10250);
    t0 = $realtime; #1.5s;
    check("#1.5s", $realtime - t0, 1.5e9);
    t0 = $realtime; #8ns;
    check("#8ns", $realtime - t0, 8);
  end
endmodule

`timescale 1ns/1ns
module tl_ns;
  real t0, r;

  task check(input string what, input real got, input real exp);
    if (got > exp + 1e-9 || got < exp - 1e-9)
      $fatal(1, "%s: got %f expected %f", what, got, exp);
  endtask

  initial begin
    r = 1.5ns;
    check("r", r, 1.5);   // Only delays are rounded
    t0 = $realtime; #1.5ns;
    check("#1.5ns", $realtime - t0, 2);
    t0 = $realtime; #2.5ns;
    check("#2.5ns", $realtime - t0, 3);
    t0 = $realtime; #1.4999ns;
    check("#1.4999ns", $realtime - t0, 1);
    t0 = $realtime; #(1.2ns + 1.2ns);
    check("#(1.2ns + 1.2ns)", $realtime - t0, 2);
    t0 = $realtime; #1500ps;
    check("#1500ps", $realtime - t0, 2);
    t0 = $realtime; #499ps;
    check("#499ps", $realtime - t0, 0);
    t0 = $realtime; #1.5;
    check("#1.5", $realtime - t0, 2);
    t0 = $realtime; #(0.6ps + 0.6ps);
    check("#(0.6ps + 0.6ps)", $realtime - t0, 0);
  end
endmodule

`timescale 1ps/1fs
module tl_fs;
  real t0;
  time tv;

  task check(input string what, input real got, input real exp);
    if (got > exp + 1e-9 || got < exp - 1e-9)
      $fatal(1, "%s: got %f expected %f", what, got, exp);
  endtask

  initial begin
    tv = 1.5ns;
    check("tv", tv, 1500);
    t0 = $realtime; #1.5ns;
    check("#1.5ns", $realtime - t0, 1500);
    t0 = $realtime; #0.1ps;
    check("#0.1ps", $realtime - t0, 0.1);
    t0 = $realtime; #0.4fs;
    check("#0.4fs", $realtime - t0, 0);
  end
endmodule

`timescale 10ns/1ns
module tl_10;
  real t0;

  task check(input string what, input real got, input real exp);
    if (got > exp + 1e-9 || got < exp - 1e-9)
      $fatal(1, "%s: got %f expected %f", what, got, exp);
  endtask

  initial begin
    t0 = $realtime; #1.5ns;
    check("#1.5ns", $realtime - t0, 0.2);
    t0 = $realtime; #1.4999ns;
    check("#1.4999ns", $realtime - t0, 0.1);
    t0 = $realtime; #1.5;
    check("#1.5", $realtime - t0, 1.5);
  end
endmodule

`timescale 1ns/1ns
module svtimelit1;
  tl_ps  u1();
  tl_ns  u2();
  tl_fs  u3();
  tl_10  u4();

  initial begin
    #2s;
    $display("PASSED");
  end
endmodule
