`timescale 1ns/1ns
// Sampled value functions at the first clock tick: $past, $rose, $fell and
// $stable before any earlier tick exists.
// Expected values: IEEE 1800-2017 16.5.1 (sampled value: at the first tick the
// value "before" is the default sampled value, i.e. the initial value of the
// variable from its declaration, x for a 4-state variable without initializer),
// 16.9.3 (sampled value functions; $rose / $fell look at the least significant
// bit and compare it with the previous sampled value), 16.12.7 (1 |-> expr).
//
// The initial values are a = 1, v = 7, bt = 1, u = x.  At 1 ns they are set to
// a = 0, v = 2, bt = 0; the only clock tick before $finish (12 ns) is the
// posedge at 5 ns, which samples a = 0, v = 2, bt = 0, u = x.  Therefore
//   $past(a) = 1, $past(v) = 7, $past(bt) = 1, $past(u) = x
//   $rose(a) = 0, $fell(a) = 1, $stable(a) = 0, $rose(bt) = 0, $fell(bt) = 1.
module sva6;
  logic clk = 0;
  logic a = 1;
  logic [3:0] v = 4'd7;
  logic u;
  bit bt = 1;
  int pass[12], fail[12];
  int errors = 0;
  always #5 clk = ~clk;
  initial begin #1 a = 0; v = 4'd2; bt = 0; end
  assert property (@(posedge clk) 1 |-> ($past(a) == 1'b1) ) pass[0]++; else fail[0]++;
  assert property (@(posedge clk) 1 |-> ($past(a) === 1'bx) ) pass[1]++; else fail[1]++;
  assert property (@(posedge clk) 1 |-> ($past(v) == 4'd7) ) pass[2]++; else fail[2]++;
  assert property (@(posedge clk) 1 |-> ($past(v) === 4'd2) ) pass[3]++; else fail[3]++;
  assert property (@(posedge clk) 1 |-> ($past(bt) == 1'b1) ) pass[4]++; else fail[4]++;
  assert property (@(posedge clk) 1 |-> ($past(u) === 1'bx) ) pass[5]++; else fail[5]++;
  assert property (@(posedge clk) 1 |-> ($past(u) === 1'b0) ) pass[6]++; else fail[6]++;
  assert property (@(posedge clk) $rose(a)) pass[7]++; else fail[7]++;
  assert property (@(posedge clk) $fell(a)) pass[8]++; else fail[8]++;
  assert property (@(posedge clk) $stable(a)) pass[9]++; else fail[9]++;
  assert property (@(posedge clk) $rose(bt)) pass[10]++; else fail[10]++;
  assert property (@(posedge clk) $fell(bt)) pass[11]++; else fail[11]++;
  // expected outcome of each assertion at the single tick: 1 = pass, 0 = fail
  function automatic bit expect_pass(int i);
    case (i)
      0: return 1;   // $past(a) == 1
      1: return 0;   // $past(a) === x
      2: return 1;   // $past(v) == 7
      3: return 0;   // $past(v) === 2
      4: return 1;   // $past(bt) == 1
      5: return 1;   // $past(u) === x
      6: return 0;   // $past(u) === 0
      7: return 0;   // $rose(a)
      8: return 1;   // $fell(a)
      9: return 0;   // $stable(a)
      10: return 0;  // $rose(bt)
      default: return 1;  // $fell(bt)
    endcase
  endfunction
  initial begin
    #12;
    for (int i = 0; i < 12; i++) begin
      if (expect_pass(i)) begin
        if (pass[i] !== 1 || fail[i] !== 0) begin
          $error("assertion %0d: %0d pass, %0d fail actions, expected 1 pass", i, pass[i], fail[i]); errors++; end
      end else begin
        if (pass[i] !== 0 || fail[i] !== 1) begin
          $error("assertion %0d: %0d pass, %0d fail actions, expected 1 fail", i, pass[i], fail[i]); errors++; end
      end
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
