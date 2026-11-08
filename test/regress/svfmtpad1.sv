// Field widths and padding of %d, %h, %b
// Expected values: IEEE 1800-2017 21.2.1.3: in decimal, leading zeros are suppressed and replaced by blanks
// (so %05d is a field of width 5 padded with blanks, there is no zero flag), %0d is the minimum width, in other
// radices leading zeros are always displayed (width = digits of the type, or the given width if larger);
// a value wider than the field is not truncated; 21.2.1.4: a value with x and z bits prints as uppercase X in decimal.
module svfmtpad1;
  int v[5] = '{5, -5, 123456, -123456, 0};
  logic [7:0] b = 8'hA5;
  logic signed [7:0] sb = -8'sd3;
  reg [3:0] x = 4'bx01z;
  int errs = 0;
  string exp[5] = '{
    "[    5] [    5] [5    ] [       5] [5] [  5] [  5]",
    "[   -5] [   -5] [-5   ] [      -5] [-5] [ -5] [ -5]",
    "[123456] [123456] [123456] [  123456] [123456] [123456] [123456]",
    "[-123456] [-123456] [-123456] [ -123456] [-123456] [-123456] [-123456]",
    "[    0] [    0] [0    ] [       0] [0] [  0] [  0]"};
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
    eq($sformatf("%05d", b), "  165", "%05d of 8'hA5");
    eq($sformatf("%05d", sb), "   -3", "%05d of -3 signed 8 bit");
    eq($sformatf("%05h", b), "000a5", "%05h");
    eq($sformatf("%05b", b), "10100101", "%05b");
    eq($sformatf("%010d", sb), "        -3", "%010d of -3");
    eq($sformatf("%5d", x), "    X", "%5d of 4'bx01z");
    $display("[%05d] [%0d] [%02d]", 3'd5, 3'd5, 8'd200);
    eq($sformatf("[%05d] [%0d] [%02d]", 3'd5, 3'd5, 8'd200), "[    5] [5] [200]", "line 7");
    if (errs == 0) $display("PASSED");
  end
endmodule
