// Flip-flop writes to memories inside an RTL island: element write log,
// overflow to a whole-memory commit, partial element writes and wakeups
module vlog_island_mem1;
  logic clk = 0;
  logic rst = 0;
  logic [7:0]  mem [64];       // 4-state, one word per element
  bit   [31:0] mem2 [32];      // 2-state
  logic [99:0] wmem [20];      // Two words per element
  logic [7:0]  rd;
  logic [5:0]  wa = 0, ra = 0;
  logic [7:0]  wd = 0;
  int          mode = 0;
  int          cyc = 0;

  always_ff @(posedge clk) begin
    cyc <= cyc + 1;
    if (rst) begin
      for (int i = 0; i < 64; i++)
        mem[i] <= 8'(i);                   // Overflows the log
    end
    else begin
      case (mode)
        0: mem[wa] <= wd;
        1: begin                            // Last write wins
          mem[wa] <= wd;
          mem[wa] <= ~wd;
        end
        2: begin                            // Two halves of one element
          mem[wa][3:0] <= wd[3:0];
          mem[wa][7:4] <= wd[7:4] ^ 4'hf;
        end
        3: for (int i = 0; i < 20; i++)     // More than the log holds
             mem[i] <= mem[i] + 8'd1;
        4: mem[wa] <= 8'bx1z0_1010;
        5: mem[wa + 1] <= mem[wa];          // Read sees the old value
        default: ;
      endcase
    end
    rd <= mem[ra];
    mem2[wa[4:0]] <= mem2[wa[4:0]] + {24'd0, wd};
    wmem[wa % 20] <= {wd, 92'h0} | 100'(wa);
  end

  int ev5 = 0, ev6 = 0;
  always @(mem[5]) ev5++;
  always @(mem[6]) ev6++;

  task automatic tick;
    #1 clk = 1;
    #1 clk = 0;
  endtask

  initial begin
    rst = 1;
    tick;
    rst = 0;
    $display("reset: %h %h %h %h", mem[0], mem[5], mem[33], mem[63]);
    mode = 0; wa = 5; wd = 8'h5a; tick;
    $display("plain: %h %h", mem[5], mem[4]);
    mode = 1; wa = 6; wd = 8'h0f; tick;
    $display("last wins: %h", mem[6]);
    mode = 1; wa = 6; wd = 8'h33; tick;
    $display("again: %h", mem[6]);
    mode = 0; wa = 6; wd = 8'hcc; tick;
    $display("same value: %h", mem[6]);
    mode = 2; wa = 7; wd = 8'h3c; tick;
    $display("halves: %h", mem[7]);
    mode = 3; tick;
    $display("loop: %h %h %h %h", mem[0], mem[5], mem[19], mem[20]);
    mode = 4; wa = 40; tick;
    $display("x/z: %b", mem[40]);
    mode = 5; wa = 40; tick;
    $display("copy: %b %b", mem[40], mem[41]);
    mode = 6; ra = 41; tick;
    $display("rd: %b", rd);
    $display("mem2: %0d %0d", mem2[5], mem2[8]);
    $display("wmem: %h %h", wmem[5], wmem[0]);
    $display("events: %0d %0d", ev5, ev6);
    rst = 1; tick; rst = 0;
    $display("reset again: %h %h %h %0d %0d", mem[5], mem[6], mem[40], ev5,
             ev6);
    $display("cycles %0d", cyc);
    $finish;
  end
endmodule
