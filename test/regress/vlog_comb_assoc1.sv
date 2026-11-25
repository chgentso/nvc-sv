module vlog_comb_assoc1;
  logic [7:0] mem[logic [7:0]];
  logic [7:0] addr1, addr2, rd1, rd2;
  logic       en;

  always_comb begin
    rd1 = 8'h00;
    rd2 = 8'h00;
    if (en && mem.exists(addr1))
      rd1 = mem[addr1];
    if (en && mem.exists(addr2))
      rd2 = mem[addr2];
  end

  initial begin
    mem[8'h10] = 8'haa;
    mem[8'h20] = 8'h55;
    en    = 1'b1;
    addr1 = 8'h10;
    addr2 = 8'h20;
    #1;
    if (rd1 !== 8'haa || rd2 !== 8'h55) begin
      $display("FAILED: rd1=%h rd2=%h", rd1, rd2);
      $finish;
    end
    addr1 = 8'h20;
    addr2 = 8'h10;
    #1;
    if (rd1 !== 8'h55 || rd2 !== 8'haa) begin
      $display("FAILED2: rd1=%h rd2=%h", rd1, rd2);
      $finish;
    end
    $display("PASSED");
  end
endmodule
