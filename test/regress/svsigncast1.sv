// Expected values: IEEE 1800-2017 6.24.1 (signed'/unsigned' casts keep the width and change only the signedness),
// 11.8.1/11.8.2 (sign extension only for signed operands), 11.4.10 (>>> is arithmetic only for a signed left operand)
module svsigncast1;
  logic [7:0] a = 8'hf0;
  logic signed [7:0] b = -8'sd16;
  int x;
  longint y;
  int err = 0;
  function automatic int f(int v); return v; endfunction
  function automatic void chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endfunction
  initial begin
    x = signed'(a);
    chk(x == -16, "signed'(a) sign-extends");
    x = unsigned'(b);
    chk(x == 240, "unsigned'(b) zero-extends");
    y = signed'(a) + 64'sd100;
    chk(y == 84, "signed'(a) + 100");
    chk(signed'(a[7:4]) == -1, "signed'(a[7:4])");
    chk(unsigned'(b[7:4]) == 15, "unsigned'(b[7:4])");
    chk(f(signed'(a)) == -16, "argument");
    chk((signed'(a) < 0) === 1'b1, "signed compare");
    chk((unsigned'(b) < 0) === 1'b0, "unsigned compare");
    chk((signed'(a) >>> 2) == -4, "arithmetic shift");
    chk((unsigned'(b) >>> 2) == 60, "logical shift of unsigned");
    if (err == 0) $display("PASSED");
  end
endmodule
