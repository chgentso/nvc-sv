// Unknown digits in the values read by $sscanf
// Expected values: IEEE 1800-2017 21.3.4.3 (%d matches digits and '_' or a single x/X/z/Z/?; %b %o %h accept x, z, ?
// digits; return value = items assigned), 5.7.1 (an unsized value whose leftmost digit is x/z is extended with x/z,
// otherwise with 0)
module svscanx1;
  reg [15:0] h;
  reg [7:0] b;
  reg [31:0] i;
  bit [7:0] t;
  integer n;
  int err = 0;
  function automatic void chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endfunction
  initial begin
    n = $sscanf("a5x7", "%h", h); chk(n == 1 && h === 16'b1010_0101_xxxx_0111, "a5x7");
    n = $sscanf("1x0z1", "%b", b); chk(n == 1 && b === 8'b0001x0z1, "1x0z1");
    n = $sscanf("xz", "%h", b); chk(n == 1 && b === 8'bxxxxzzzz, "xz hex");
    n = $sscanf("zzzz", "%h", h); chk(n == 1 && h === 16'hzzzz, "zzzz");
    n = $sscanf("x", "%b", b); chk(n == 1 && b === 8'bxxxxxxxx, "x binary, x-extended");
    n = $sscanf("1?0", "%b", b); chk(n == 1 && b === 8'b000001z0, "? is z");
    n = $sscanf("X7", "%h", b); chk(n == 1 && b === 8'bxxxx0111, "X7");
    n = $sscanf("1x", "%o", b); chk(n == 1 && b === 8'b00001xxx, "octal 1x");
    n = $sscanf("1x", "%d", b); chk(n == 1 && b === 8'd1, "%d stops at x");
    n = $sscanf("x", "%d", i); chk(n == 1 && i === 32'bx, "%d single x");
    n = $sscanf("fg", "%h", b); chk(n == 1 && b === 8'h0f, "%h stops at g");
    n = $sscanf("1_0", "%b", b); chk(n == 1 && b === 8'b00000010, "underscore in %b");
    n = $sscanf("1010 f", "%b %h", b, h); chk(n == 2 && b === 8'b00001010 && h === 16'h000f, "two items");
    n = $sscanf("12ab", "%d%h", b, h); chk(n == 2 && b === 8'd12 && h === 16'h00ab, "%d%h");
    if (err == 0) $display("PASSED");
  end
endmodule
