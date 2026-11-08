// %d / %0d of vectors wider than 64 bits.
// Expected values: IEEE 1800-2017 21.2.1.3 (%d prints a decimal value; %0d has no padding; %d
// without a width pads to the width of the largest value of the type; signed values print a minus
// sign), 6.24.1 ($signed), 11.8.
module svwidedec1;
  logic [99:0] a = 100'h8_0000_0000_0000_0000_0000_0000;
  logic signed [127:0] b = -128'sd1;
  logic [127:0] c = '1;
  logic [199:0] d = 200'h0123456789abcdef0123456789abcdef0123456789abcdef01;
  logic signed [149:0] e = -150'sd123456789012345678901234567890;
  logic [64:0] f = 65'd10000000000000000000;
  logic [129:0] g = 130'd10000000000000000000;
  logic [129:0] h = 0;
  logic signed [191:0] i = {1'b1, 191'd0};
  logic [255:0] j = 256'd10000000000000000000000000000000000000;
  int errors = 0;
  string s;
  task automatic chk(string got, string exp, string what);
    if (got != exp) begin $error("%s: got '%s' exp '%s'", what, got, exp); errors++; end
  endtask
  initial begin
    $sformat(s, "%0d", a); chk(s, "633825300114114700748351602688", "a");
    $sformat(s, "%0d", b); chk(s, "-1", "b");
    $sformat(s, "%0d", c); chk(s, "340282366920938463463374607431768211455", "c");
    $sformat(s, "%0d", d); chk(s, "7141946863373290020600059860972015138662662921272173653761", "d");
    $sformat(s, "%0d", e); chk(s, "-123456789012345678901234567890", "e");
    $sformat(s, "%0d %0d %0d", f, g, h); chk(s, "10000000000000000000 10000000000000000000 0", "f g h");
    $sformat(s, "%0d", i); chk(s, "-3138550867693340381917894711603833208051177722232017256448", "i");
    $sformat(s, "%0d", j); chk(s, "10000000000000000000000000000000000000", "j");
    // %d pads to the number of digits of the largest 256-bit value (78 digits)
    $sformat(s, "%d", j); chk(s, "                                        10000000000000000000000000000000000000", "j padded");
    $sformat(s, "%40d|", f); chk(s, "                    10000000000000000000|", "f in 40 columns");
    $sformat(s, "%0d", $signed(c)); chk(s, "-1", "signed c");
    if (errors == 0) $display("PASSED");
  end
endmodule
