// Field widths and padding of %d, %h, %b
// Expected values: IEEE 1800-2017 21.2.1.3: in decimal, leading zeros are suppressed and replaced by blanks,
// but a width with a leading zero (%05d) fills the field with zeros after the sign as C does and other
// simulators do (not with the - flag, nor a value with x or z bits), %0d is the minimum width, in other
// radices leading zeros are always displayed (width = digits of the type, or the given width if larger);
// a value wider than the field is not truncated; 21.2.1.4: a value with x and z bits prints as uppercase X in decimal.
module svfmtpad1;
  int v[5] = '{5, -5, 123456, -123456, 0};
  logic [7:0] b = 8'hA5;
  logic signed [7:0] sb = -8'sd3;
  reg [3:0] x = 4'bx01z;
  int errs = 0;
  string exp[5] = '{
    "[00005] [    5] [5    ] [00000005] [5] [005] [  5]",
    "[-0005] [   -5] [-5   ] [-0000005] [-5] [-05] [ -5]",
    "[123456] [123456] [123456] [00123456] [123456] [123456] [123456]",
    "[-123456] [-123456] [-123456] [-0123456] [-123456] [-123456] [-123456]",
    "[00000] [    0] [0    ] [00000000] [0] [000] [  0]"};
  task automatic eq(string got, string e, string m);
    if (got != e) begin $error("%s: got '%s' expected '%s'", m, got, e); errs++; end
  endtask
  initial begin
    string t;
    foreach (v[i]) begin
      $display("[%05d] [%5d] [%-5d] [%08d] [%0d] [%03d] [%3d]", v[i], v[i], v[i], v[i], v[i], v[i], v[i]);
      t = $sformatf("[%05d] [%5d] [%-5d] [%08d] [%0d] [%03d] [%3d]", v[i], v[i], v[i], v[i], v[i], v[i], v[i]);
      eq(t, exp[i], $sformatf("row %0d", i));
    end
    $display("[%05d] [%05d] [%05h] [%05b] [%010d] [%5d]", b, sb, b, b, sb, x);
    eq($sformatf("%05d", b), "00165", "%05d of 8'hA5");
    eq($sformatf("%05d", sb), "-0003", "%05d of -3 signed 8 bit");
    eq($sformatf("%05h", b), "000a5", "%05h");
    eq($sformatf("%05b", b), "10100101", "%05b");
    eq($sformatf("%010d", sb), "-000000003", "%010d of -3");
    eq($sformatf("%5d", x), "    X", "%5d of 4'bx01z");
    $display("[%05d] [%0d] [%02d]", 3'd5, 3'd5, 8'd200);
    eq($sformatf("[%05d] [%0d] [%02d]", 3'd5, 3'd5, 8'd200), "[00005] [5] [200]", "line 7");
    if (errs == 0) $display("PASSED");
  end
endmodule
