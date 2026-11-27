// Registers of an island flip-flop domain committed as runs: written in
// another order than declared, mixed 2/4-state and widths, some read by
// combinational logic, some by processes outside the island
module vlog_island_runs1;
  logic clk = 0;
  logic [7:0]  r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0;
  bit   [15:0] t0, t1;
  logic [99:0] w0 = 0;
  logic [7:0]  c0 = 0, c1 = 0;
  logic [7:0]  sum;
  logic [7:0]  seed = 8'h11;

  always_ff @(posedge clk) begin
    r5 <= r4 + 8'd5;
    r0 <= seed;
    r3 <= r2 ^ 8'h3c;
    t1 <= t0 + 16'd7;
    r1 <= r0 + 8'd1;
    w0 <= {w0[98:0], r5[0]};
    c1 <= c0;
    r4 <= r3;
    t0 <= {8'h0, r1};
    r2 <= r1 << 1;
    c0 <= r5 ^ c1;
  end

  logic [7:0]  chk;

  always_comb sum = c0 + c1;           // Combinational readers
  always_comb chk = r5 ^ t1[7:0] ^ w0[7:0];

  initial begin
    repeat (6) begin
      #1 clk = 1;
      #1 clk = 0;
      $display("%h %h", chk, sum);
      seed = seed + 8'h22;
    end
    $finish;
  end
endmodule
