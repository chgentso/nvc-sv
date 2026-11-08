// Wildcard equality operators ==? and !=?
// Expected values: IEEE 1800-2017 11.4.6: x and z bits in the RIGHT operand are wildcards; any
// other bit comparing unequal (or x/z in the left operand against a known right bit) is a mismatch
// only when both are known: a definite mismatch gives 0, otherwise an x/z in the left operand
// against a non-wildcard right bit gives x, otherwise 1.  !=? is the logical negation (!x = x).
// 11.8.2: operands are zero-extended to the larger width (unsigned); 20.6.2 ($bits).
module svwildeq1;
  logic [3:0] a = 4'b10x1, b = 4'b1z01;
  logic [7:0] w = 8'b1010_zx01;
  logic [99:0] big = {20'hx, 60'h0, 20'h5};
  int i = 7;
  int errors = 0;
  int ln = 0;
  task automatic chk(logic got, logic exp);
    ln++;
    if (got !== exp) begin $error("check %0d: got %b exp %b", ln, got, exp); errors++; end
  endtask
  initial begin
    chk(a ==? 4'b10?1, 1'b1);
    chk(a !=? 4'b01?1, 1'b1);
    chk(a ==? 4'b1001, 1'bx);
    chk(a !=? 4'b1001, 1'bx);
    chk(a ==? 4'b0xx1, 1'b0);
    chk(a ==? 4'bxxxx, 1'b1);
    chk(b ==? 4'b1001, 1'bx);
    chk(b ==? 4'b1?01, 1'b1);
    chk(4'b1010 ==? 4'b10x0, 1'b1);
    chk(4'b1010 !=? 4'b11x0, 1'b1);
    chk(w ==? 8'b1010_xx01, 1'b1);
    chk(w ==? 8'b1010_0001, 1'bx);
    chk(i ==? 32'b????_????_????_????_????_????_????_?111, 1'b1);
    chk(i !=? 32'b0, 1'b1);
    chk(big ==? {20'bx, 60'h0, 20'h5}, 1'b1);
    chk(big ==? {20'h0, 60'h0, 20'h5}, 1'bx);
    chk(big ==? {20'hx, 60'hx, 20'h4}, 1'b0);
    if (((a ==? 4'b10?1) ? 5 : 6) != 5) begin $error("ternary"); errors++; end
    chk((4'b1000 ==? 4'b1??? ) && (4'b0000 !=? 4'b1???), 1'b1);
    begin
      automatic bit taken = 0, else_taken = 0;
      if (a ==? 4'b10?1) taken = 1;
      if (a ==? 4'b1000) begin errors++; $error("bad: x condition taken"); end else else_taken = 1;
      if (!taken || !else_taken) begin $error("if/else %b %b", taken, else_taken); errors++; end
    end
    chk(3'b101 ==? 5'b??101, 1'b1);
    chk(5'b11101 ==? 3'b?01, 1'b0);
    if ($bits(a ==? a) != 1) begin $error("bits"); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
