// Bin values that are not enumerators of an enum coverpoint
// Expected values: IEEE 1800-2017 19.5.7 (bin values are evaluated in the type of the coverpoint
// expression; for an enum the base type applies, so non-member values give valid bins),
// 19.5.1 (bins[]), 19.11 (coverage = covered/total bins, default bin not counted, group =
// average of its coverpoints). s is a 16-bit enum (S0=1, S1=2, S2=4, S3=8), e an int enum.
module svcovenum2;
  typedef enum bit [15:0] {S0=1, S1=2, S2=4, S3=8} sl_e;
  typedef enum {A=-1, B=0, C=5} se_e;
  sl_e s;
  se_e e;
  int errs = 0;
  covergroup cg;
    c1: coverpoint s { bins B[] = {[0:2]}; bins C = {4}; }
    c2: coverpoint s { bins Z = {0}; bins Y = {1, 7}; bins R = {[3:20]}; }
    c3: coverpoint e { bins N[] = {[-3:6]}; }
    c4: coverpoint s { bins X[] = {1, 3, 8}; bins D = default; }
  endgroup
  cg c = new;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  task automatic chk5(string m, real g, real e1, real e2, real e3, real e4);
    chkr({m, " group"}, c.get_coverage(), g);
    chkr({m, " c1"}, c.c1.get_coverage(), e1);
    chkr({m, " c2"}, c.c2.get_coverage(), e2);
    chkr({m, " c3"}, c.c3.get_coverage(), e3);
    chkr({m, " c4"}, c.c4.get_coverage(), e4);
  endtask
  initial begin
    // bins: c1 = B[0],B[1],B[2],C (4); c2 = Z,Y,R (3); c3 = N[-3..6] (10); c4 = X[0..2] (3)
    chkr("0", c.get_coverage(), 0.0);
    s = S1; e = A; c.sample();      // s=2, e=-1
    chk5("1", (25.0 + 0.0 + 10.0 + 0.0) / 4, 25.0, 0.0, 10.0, 0.0);
    s = S2; e = C; c.sample();      // s=4, e=5
    chk5("2", (50.0 + 100.0/3 + 20.0 + 0.0) / 4, 50.0, 100.0/3, 20.0, 0.0);
    s = S0; e = B; c.sample();      // s=1, e=0
    chk5("3", (75.0 + 200.0/3 + 30.0 + 100.0/3) / 4, 75.0, 200.0/3, 30.0, 100.0/3);
    if (errs == 0) $display("PASSED");
  end
endmodule
