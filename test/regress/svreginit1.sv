// Self-checking: an output reg which is read by a continuous assignment in its own module is
// still an unassigned 4-state variable, so the net it drives is x and not z before the first
// assignment.  IEEE 1800-2017 6.8 (default value of a 4-state variable is x), 23.3.3.
module svreginit1_sub(input clk, output en, output reg [1:0] state);
  assign en = (state == 2'd1);
  always @(posedge clk) state <= 2'd1;
endmodule

module svreginit1;
  reg clk = 0;
  wire [1:0] w;
  wire en;
  int errors;

  svreginit1_sub u(.clk(clk), .en(en), .state(w));

  initial begin
    #1;
    if (w !== 2'bxx) begin
      $display("FAIL: w = %b before the clock", w);
      errors++;
    end
    if (en !== 1'bx) begin
      $display("FAIL: en = %b before the clock", en);
      errors++;
    end
    #1 clk = 1;
    #1;
    if (w !== 2'd1 || en !== 1'b1) begin
      $display("FAIL: w = %b en = %b after the clock", w, en);
      errors++;
    end
    if (errors == 0) $display("PASSED");
    else $fatal(1, "errors");
  end
endmodule
