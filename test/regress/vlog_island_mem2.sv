// A large memory written and read every cycle by an island flip-flop
module vlog_island_mem2;
  bit clk = 0;
  logic [31:0] mem [65536];
  logic [31:0] lfsr = 32'h1234_5678;
  logic [31:0] acc = 0;
  logic [31:0] sum = 0;
  int cyc = 0;

  always #1 clk = ~clk;

  function automatic logic [31:0] next_lfsr(logic [31:0] x);
    x = x ^ (x << 13);
    x = x ^ (x >> 17);
    return x ^ (x << 5);
  endfunction

  always_ff @(posedge clk) begin
    lfsr <= next_lfsr(lfsr);
    if (cyc < 64)
      mem[cyc[5:0] * 1024 + 3] <= 32'(cyc);
    else begin
      if (^mem[lfsr[15:0]] === 1'bx)
        acc <= acc + 32'd7;
      else begin
        acc <= acc + mem[lfsr[15:0]];
        sum <= sum + 1;
      end
      mem[(lfsr >> 7) & 16'hffff] <= acc ^ lfsr;
    end
    cyc <= cyc + 1;
  end

  initial begin
    #(40000);
    $display("cycles=%0d acc=%h sum=%0d m3=%h m1027=%h", cyc, acc, sum,
             mem[3], mem[1027]);
    $finish;
  end
endmodule
