// Self-checking: an edge of an expression which is not a signal, as in always_ff @(posedge clk or
// posedge !rst_n), is an edge of the least significant bit of its value.
// Expected values: IEEE 1800-2017 9.4.2.1 (event control with edge on an expression), 9.4.2.2.
module svedgeexpr1;
  logic clk = 0, rst_n = 0, a = 0, b = 1;
  logic [3:0] cnt = 4;
  int async_resets, a_and_b_rise, a_and_b_fall, odd_rise;
  bit q;

  always #5 clk = ~clk;

  // Asynchronous reset written as an active high expression
  always_ff @(posedge clk or posedge !rst_n)
    if (!rst_n) q <= 0;
    else q <= 1;

  always @(posedge !rst_n) async_resets++;
  always @(posedge (a & b)) a_and_b_rise++;
  always @(negedge (a & b)) a_and_b_fall++;
  // Least significant bit of cnt + 1 goes high when cnt changes from odd to even
  always @(posedge (cnt + 1)) odd_rise++;

  // The net holding the value of an expression takes its first value at time zero
  initial begin
    #1 async_resets = 0; a_and_b_rise = 0; a_and_b_fall = 0; odd_rise = 0;
  end

  initial begin
    #12 rst_n = 1;                  // no edge of !rst_n upward
    #10 if (q !== 1'b1) $error("q %b after reset released", q);
    a = 1;                          // a & b rises
    #3 a = 0;                       // falls
    #3 b = 0;                       // a & b stays low
    #3 a = 1;                       // still low
    #3 b = 1;                       // rises: a is 1
    #3 cnt = 5;                     // cnt + 1 = 6: LSB 0, no posedge
    #3 cnt = 6;                     // cnt + 1 = 7: LSB 1, posedge
    #3 cnt = 8;                     // cnt + 1 = 9: LSB stays 1, no event
    #3 rst_n = 0;                   // !rst_n rises: async reset
    #1 if (q !== 1'b0) $error("q %b after async reset", q);
    #5 rst_n = 1;
    #20;
    if (async_resets == 1 && a_and_b_rise == 2 && a_and_b_fall == 1 && odd_rise == 1)
      $display("PASSED");
    else
      $error("resets %0d rise %0d fall %0d odd %0d, expected 1 2 1 1", async_resets, a_and_b_rise, a_and_b_fall,
             odd_rise);
    $finish;
  end
endmodule
