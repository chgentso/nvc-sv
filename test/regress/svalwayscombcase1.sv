`timescale 1ns/1ns
// always_comb and always @* containing a case (1'b1) with bit-select items and
// a default, driven by changes of a selector vector and of a data input.
// Expected values: IEEE 1800-2017 9.2.2.2 (always_comb: executes once at time
// zero, re-executes on any change of a value it reads), 9.2.1 (always @*),
// 12.5 (case: first matching item in order; case (1'b1) compares the expression
// with each item using ===, so an item that is x or 0 does not match),
// 12.5 (unique case: exactly one item matches or there is a default).
// Order of events with #1 steps: hit=000 -> default ('1 = f), hit=001 -> a (1),
// hit=010 -> b (2), b = 7 while hit=010 -> 7, hit=000 -> default again.
module svalwayscombcase1;
  logic [2:0] hit;
  logic [3:0] a = 4'd1, b = 4'd2;
  logic [3:0] r, s;
  int errors = 0;
  always_comb begin
    r = '0;
    unique case (1'b1)
      hit[0]: r = a;
      hit[1]: r = b;
      default: r = '1;
    endcase
  end
  always @* begin
    case (1'b1)
      hit[0]: s = a;
      hit[1]: s = b;
      default: s = '1;
    endcase
  end
  task automatic chk(int step, logic [3:0] er, logic [3:0] es);
    if (r !== er || s !== es) begin
      $error("step %0d: r = %h s = %h, expected %h %h", step, r, s, er, es); errors++;
    end
  endtask
  initial begin
    hit = 3'b000; #1 chk(1, 4'hf, 4'hf);
    hit = 3'b001; #1 chk(2, 4'h1, 4'h1);
    hit = 3'b010; #1 chk(3, 4'h2, 4'h2);
    b = 4'd7; #1 chk(4, 4'h7, 4'h7);
    hit = 3'b000; #1 chk(5, 4'hf, 4'hf);
    if (errors == 0) $display("PASSED");
  end
endmodule
