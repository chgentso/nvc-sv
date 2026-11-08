// IEEE 1800-2017 7.4.1: a packed array is an integral value; 21.2.1.7:
// %p prints an integral value in decimal.  An array with several packed
// dimensions is therefore printed as one number, the same as a vector
// with a single packed dimension and the same bits, also as an element of
// an unpacked array or a member of an unpacked structure.
module svpfmtpacked1;
  bit [1:0][3:0] p2 = 8'ha5;
  logic [1:0][1:0][1:0] p3 = 8'hc3;
  logic [1:0][3:0] px = 8'b1x00_0011;
  bit signed [1:0][3:0] ps = 8'hf0;
  bit [7:0] v1 = 8'ha5;
  logic [7:0] vx = 8'b1x00_0011;
  bit signed [7:0] vs = 8'hf0;
  bit [1:0][3:0] arr[2] = '{8'h12, 8'h34};
  struct { bit [1:0][3:0] m; int i; } st = '{8'h21, 7};
  int errors = 0;

  task check(string got, string want);
    if (got != want) begin
      $display("got '%s', expected '%s'", got, want);
      errors++;
    end
  endtask

  initial begin
    check($sformatf("%p", p2), "165");
    check($sformatf("%p", p2), $sformatf("%p", v1));
    check($sformatf("%p", p3), "195");
    check($sformatf("%p", px), $sformatf("%p", vx));
    check($sformatf("%p", ps), $sformatf("%p", vs));
    check($sformatf("%p", ps), "-16");
    check($sformatf("%0p", p2), "165");
    check($sformatf("%p", arr), "'{18, 52}");
    check($sformatf("%p", st), "'{m:33, i:7}");
    if (errors == 0) $display("PASSED");
  end
endmodule
