// Self-checking. IEEE 1800-2017 11.4.13: a $ in a range of an inside
// expression stands for the smallest or largest value of the expression.
module svinsidedollar1;
  int value = 50;
  logic [3:0] u = 4'hf;
  bit r1, r2, r3, r4, r5, c;
  initial begin
    r1 = value inside {[$:$]};
    r2 = value inside {[$:40]};
    r3 = value inside {[60:$]};
    r4 = u inside {[$:3]};
    r5 = u inside {[14:$]};
    if ({r1, r2, r3, r4, r5} !== 5'b10001) $error("FAIL %b", {r1, r2, r3, r4, r5});
    case (value) inside
      [$:49]: c = 0;
      [50:$]: c = 1;
      default: c = 0;
    endcase
    if (c !== 1) $error("FAIL case");
    $display("PASSED");
  end
endmodule
