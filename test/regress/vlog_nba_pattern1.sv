// Non-blocking assignment of an assignment pattern to an unpacked array
module vlog_nba_pattern1;
  logic [7:0]  a [4];
  logic [7:0]  b [4];
  logic [3:0]  m [2][3];
  bit   [15:0] w [3];
  logic        clk = 0;
  logic [7:0]  r [32];
  int          mode = 0;

  always_ff @(posedge clk) begin
    if (mode == 1)
      r <= '{default: 8'h77};
    else if (mode == 2) begin
      r <= '{0: 8'h11, 31: 8'h1f, default: 8'h22};
      r[5] <= 8'h55;                    // Last write wins
    end
    else
      r[mode] <= 8'(mode);
  end

  initial begin
    a <= '{default: 8'h77};
    b <= '{8'h1, 8'h2, 8'h3, 8'h4};
    m <= '{'{1, 2, 3}, '{4, 5, 6}};
    w <= '{0: 16'haaaa, default: 16'h5555};
    #1;
    $display("%h %h %h %h", a[0], a[1], a[2], a[3]);
    $display("%h %h %h %h", b[0], b[1], b[2], b[3]);
    $display("%h %h %h %h %h %h", m[0][0], m[0][1], m[0][2], m[1][0],
             m[1][1], m[1][2]);
    $display("%h %h %h", w[0], w[1], w[2]);
    mode = 3; #1 clk = 1; #1 clk = 0;
    $display("%h %h", r[3], r[4]);
    mode = 1; #1 clk = 1; #1 clk = 0;
    $display("%h %h %h", r[0], r[3], r[31]);
    mode = 2; #1 clk = 1; #1 clk = 0;
    $display("%h %h %h %h", r[0], r[1], r[5], r[31]);
    $finish;
  end
endmodule
