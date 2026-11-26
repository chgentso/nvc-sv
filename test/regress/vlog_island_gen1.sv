// Test SV generate-block fusion in RTL islands:
//  1) Regfile + write-enable decoder (disjoint constant-index writes across
//     for-generate iterations + top-level mem[0] + if-generate branch)
//  2) Nested if/for generates + generate-local signals (with wave visibility)
//  3) Disjoint constant-comb + FF writes on the same vector (CSR counter style)
//  4) Overlapping multi-driver across generate iterations safely ejected

module gen_regfile #(
  parameter int N = 8,
  parameter int USE_ALT = 0
) (
  input  logic        clk,
  input  logic        rst_n,
  input  logic [2:0]  raddr_a,
  input  logic [2:0]  raddr_b,
  input  logic [2:0]  waddr,
  input  logic [15:0] wdata,
  input  logic        we,
  output logic [15:0] rdata_a,
  output logic [15:0] rdata_b,
  output logic [15:0] alt_o
);
  logic [N-1:0][15:0] mem;
  logic [N-1:0]       we_dec;

  assign rdata_a = mem[raddr_a];
  assign rdata_b = mem[raddr_b];

  genvar g;
  generate
    for (g = 0; g < N; g++) begin : gen_we_dec
      assign we_dec[g] = (waddr == g) ? we : 1'b0;
    end
  endgenerate

  generate
    always_ff @(posedge clk or negedge rst_n) begin
      if (!rst_n) mem[0] <= 16'h0000;
      else        mem[0] <= 16'h0000;
    end

    for (g = 1; g < N; g++) begin : gen_rf
      always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)          mem[g] <= 16'h0000;
        else if (we_dec[g])  mem[g] <= wdata;
      end
    end

    if (USE_ALT == 1) begin : gen_alt_on
      assign alt_o = mem[1] ^ 16'hFFFF;
    end else begin : gen_alt_off
      assign alt_o = 16'h5A5A;
    end
  endgenerate
endmodule

module gen_nested_local #(
  parameter int W = 8
) (
  input  logic [W-1:0] a_i,
  input  logic [W-1:0] b_i,
  output logic [W-1:0] rev_o,
  output logic [W-1:0] pair_o
);
  genvar i;
  generate
    for (i = 0; i < W; i++) begin : gen_bit
      logic tmp;
      if (i % 2 == 0) begin : gen_even
        logic inner;
        assign inner = a_i[W-1-i] ^ b_i[i];
        assign tmp   = inner;
      end else begin : gen_odd
        assign tmp = a_i[W-1-i] & b_i[i];
      end
      assign rev_o[i] = tmp;
    end

    for (i = 0; i < W/2; i++) begin : gen_pair
      assign pair_o[2*i+1:2*i] = {a_i[2*i], b_i[2*i+1]};
    end
  endgenerate
endmodule

module gen_mixed_tieoff (
  input  logic            clk,
  input  logic            rst_n,
  input  logic [3:0]      en_i,
  output logic [3:0][7:0] cnt_o
);
  logic [3:0][7:0] cnt_q;

  genvar k;
  generate
    for (k = 0; k < 4; k++) begin : gen_cnt
      if (k == 1 || k == 3) begin : gen_tie
        assign cnt_q[k] = 8'h00;
      end else begin : gen_ff
        always_ff @(posedge clk or negedge rst_n) begin
          if (!rst_n)     cnt_q[k] <= 8'h00;
          else if (en_i[k]) cnt_q[k] <= cnt_q[k] + 8'h01;
        end
      end
    end
  endgenerate

  assign cnt_o = cnt_q;
endmodule

module gen_overlap_reject (
  input  logic [3:0] d_i,
  output wire  [1:0] y_o
);
  genvar m;
  generate
    // Iterations 0 & 1 both drive y_o[0] -> must be safely ejected (multi-driver)
    for (m = 0; m < 4; m++) begin : gen_ov
      assign y_o[m / 2] = d_i[m];
    end
  endgenerate
endmodule

module vlog_island_gen1;
  logic        clk = 0;
  logic        rst_n = 0;
  logic [2:0]  raddr_a = 0, raddr_b = 0, waddr = 0;
  logic [15:0] wdata = 0;
  logic        we = 0;
  logic [15:0] rdata_a, rdata_b, alt_o;

  gen_regfile #(8, 0) u_rf (.*);

  logic [7:0] a_i = 8'hA5, b_i = 8'h3C;
  logic [7:0] rev_o, pair_o;

  gen_nested_local #(8) u_nl (.*);

  logic [3:0]      en_i = 4'b0000;
  logic [3:0][7:0] cnt_o;

  gen_mixed_tieoff u_cnt (.*);

  logic [3:0] ov_d = 4'b1100;
  wire  [1:0] ov_y;

  gen_overlap_reject u_ov (.d_i(ov_d), .y_o(ov_y));

  always #5 clk = ~clk;

  initial begin
    #2;
    if (alt_o !== 16'h5A5A) $fatal(1, "alt_o mismatch: %h", alt_o);
    if (rdata_a !== 16'h0000) $fatal(1, "rf rst mismatch: %h", rdata_a);
    if (cnt_o[0] !== 8'h00 || cnt_o[1] !== 8'h00 || cnt_o[2] !== 8'h00 || cnt_o[3] !== 8'h00)
      $fatal(1, "cnt rst mismatch");

    // Check nested generate + local signals
    // a_i = 1010_0101, b_i = 0011_1100
    // rev(a_i) = 1010_0101
    // even i (0,2,4,6): rev_a[i] ^ b[i]
    //   i=0: 1^0=1, i=2: 1^1=0, i=4: 0^1=1, i=6: 0^0=0
    // odd  i (1,3,5,7): rev_a[i] & b[i]
    //   i=1: 0&0=0, i=3: 0&1=0, i=5: 1&1=1, i=7: 1&0=0
    // rev_o = 8'b0011_0001 = 8'h31
    if (rev_o !== 8'h31) $fatal(1, "rev_o mismatch: got %b exp 00110001", rev_o);
    // pair_o[2i+1:2i] = {a_i[2i], b_i[2i+1]}
    //   i=0: {1,0}=10, i=1: {1,1}=11, i=2: {0,1}=01, i=3: {0,0}=00 -> 8'b00_01_11_10 = 8'h1E
    if (pair_o !== 8'h1E) $fatal(1, "pair_o mismatch: got %b exp 00011110", pair_o);

    // Check overlapping multi-driver (both drivers agree on 0 for [0] and 1 for [1])
    if (ov_y !== 2'b10) $fatal(1, "ov_y agree mismatch: %b", ov_y);
    ov_d = 4'b1001;
    #1;
    if (ov_y !== 2'bxx) $fatal(1, "ov_y conflict mismatch: %b", ov_y);

    // Write registers 0 (ignored), 3, 7 and increment counters
    rst_n = 1;
    en_i  = 4'b1111;
    we    = 1; waddr = 3'd0; wdata = 16'hDEAD;
    @(posedge clk); #1;
    waddr = 3'd3; wdata = 16'h1234;
    @(posedge clk); #1;
    waddr = 3'd7; wdata = 16'hBEEF;
    @(posedge clk); #1;
    we = 0;

    raddr_a = 3'd0; raddr_b = 3'd3;
    #1;
    if (rdata_a !== 16'h0000 || rdata_b !== 16'h1234)
      $fatal(1, "rf read1 mismatch: a=%h b=%h", rdata_a, rdata_b);

    raddr_a = 3'd7; raddr_b = 3'd2;
    #1;
    if (rdata_a !== 16'hBEEF || rdata_b !== 16'h0000)
      $fatal(1, "rf read2 mismatch: a=%h b=%h", rdata_a, rdata_b);

    // Counters 0 and 2 incremented 3 times; 1 and 3 tied to 0
    if (cnt_o[0] !== 8'h03 || cnt_o[1] !== 8'h00 || cnt_o[2] !== 8'h03 || cnt_o[3] !== 8'h00)
      $fatal(1, "cnt mismatch: %h %h %h %h", cnt_o[0], cnt_o[1], cnt_o[2], cnt_o[3]);

    $display("PASSED");
    $finish;
  end
endmodule
