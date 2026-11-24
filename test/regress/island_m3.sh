set -xe

# 1. Pipeline, counter, NBA swap (a <= b; b <= a;), FF-only island, and diff vs off
cat >ff_swap_pipe.sv <<'EOF'
module ff_swap_unit (
  input  logic       clk,
  input  logic [7:0] din,
  output logic [7:0] a_o,
  output logic [7:0] b_o,
  output bit   [7:0] cnt_o
);
  logic [7:0] a_q = 8'h11;
  logic [7:0] b_q = 8'h22;
  logic [7:0] stage1_q = 8'h00;
  bit   [7:0] cnt_q = 8'h00;

  always_ff @(posedge clk) begin
    a_q      <= b_q;
    b_q      <= a_q ^ din;
    stage1_q <= din;
  end

  always_ff @(posedge clk) begin
    cnt_q <= cnt_q + 8'd1;
  end

  assign a_o   = a_q;
  assign b_o   = b_q ^ stage1_q;
  assign cnt_o = cnt_q;
endmodule

module tb_ff_swap;
  logic       clk = 0;
  logic [7:0] din = 8'h05, a_o, b_o;
  bit   [7:0] cnt_o;

  ff_swap_unit dut (.*);

  initial begin
    repeat (10) begin
      #5 clk = 1;
      // Pre-NBA check in active region must see previous cycle values?
      #5 clk = 0;
      $display("CYC cnt=%0d a=%h b=%h", cnt_o, a_o, b_o);
      din = din + 8'h13;
    end
    $display("PASSED ff_swap cnt=%0d a=%h b=%h", cnt_o, a_o, b_o);
    $finish;
  end
endmodule
EOF

nvc-sv -a ff_swap_pipe.sv -e --rtl-islands=module tb_ff_swap
nvc-sv -r --wave=swap_m3.fst tb_ff_swap > swap_m3.out
grep -q "PASSED ff_swap" swap_m3.out

nvc-sv -a ff_swap_pipe.sv -e --rtl-islands=off tb_ff_swap
nvc-sv -r --wave=swap_off.fst tb_ff_swap > swap_off.out
diff -u swap_off.out swap_m3.out

# 2. Async reset (negedge rst_n + posedge rst), simultaneous clk+rst edge, and FF-only
cat >async_rst.sv <<'EOF'
module async_rst_unit (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       rst_p,
  input  logic [7:0] d,
  output logic [7:0] q_n,
  output logic [7:0] q_p
);
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      q_n <= 8'ha5;
    else
      q_n <= d;
  end

  always_ff @(posedge clk or posedge rst_p) begin
    if (rst_p)
      q_p <= 8'h5a;
    else
      q_p <= d ^ 8'hff;
  end
endmodule

module tb_async_rst;
  logic clk = 0, rst_n = 1, rst_p = 0;
  logic [7:0] d = 8'h33, q_n, q_p;

  async_rst_unit dut (.*);

  initial begin
    #2 rst_n = 0; rst_p = 1;
    #1;
    if (q_n !== 8'ha5 || q_p !== 8'h5a) begin
      $display("FAILED async_rst edge q_n=%h q_p=%h", q_n, q_p);
      $finish;
    end
    #2 rst_n = 1; rst_p = 0;
    #5 clk = 1; #5 clk = 0;
    #1;
    if (q_n !== 8'h33 || q_p !== 8'hcc) begin
      $display("FAILED async_rst clk q_n=%h q_p=%h", q_n, q_p);
      $finish;
    end

    // Simultaneous posedge clk and negedge rst_n (same delta!)
    d = 8'h77;
    #4;
    clk = 1;
    rst_n = 0;
    rst_p = 1;
    #1;
    if (q_n !== 8'ha5 || q_p !== 8'h5a) begin
      $display("FAILED async_rst simul q_n=%h q_p=%h", q_n, q_p);
      $finish;
    end
    $display("PASSED async_rst q_n=%h q_p=%h", q_n, q_p);
    $finish;
  end
endmodule
EOF

nvc-sv -a async_rst.sv -e --rtl-islands=module tb_async_rst
nvc-sv -r tb_async_rst > rst_m3.out
grep -q "PASSED async_rst" rst_m3.out
nvc-sv -a async_rst.sv -e --rtl-islands=off tb_async_rst
nvc-sv -r tb_async_rst > rst_off.out
diff -u rst_off.out rst_m3.out

# 3. Partial-select NBAs, wide >64-bit partial NBAs, and unpacked array + struct NBAs
cat >partial_mem_nba.sv <<'EOF'
module partial_mem_unit (
  input  logic        clk,
  input  logic        rst_n,
  input  logic        we,
  input  logic [1:0]  wptr,
  input  logic [1:0]  rptr,
  input  logic [3:0]  nib_lo,
  input  logic [3:0]  nib_hi,
  input  logic [31:0] wdata,
  input  logic [3:0]  wtag,
  output logic [7:0]  q_byte,
  output logic [95:0] wide_q,
  output logic [31:0] rdata,
  output logic [3:0]  rtag
);
  typedef struct packed {
    logic [31:0] data;
    logic [3:0]  tag;
    logic        valid;
  } slot_t;

  slot_t mem [0:3];
  logic [7:0]  byte_q;
  logic [95:0] wide_reg;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      byte_q   <= 8'h00;
      wide_reg <= 96'h0;
      for (int i = 0; i < 4; i++) begin
        mem[i].data  <= 32'h0;
        mem[i].tag   <= 4'h0;
        mem[i].valid <= 1'b0;
      end
    end else begin
      if (we) begin
        byte_q[3:0]     <= nib_lo;
        byte_q[7:4]     <= nib_hi;
        wide_reg[31:0]  <= wdata;
        wide_reg[95:64] <= {28'hCAFE_BEE, wtag};
        mem[wptr].data  <= wdata;
        mem[wptr].tag   <= wtag;
        mem[wptr].valid <= 1'b1;
      end else begin
        // Only lower nibble updates; upper nibble must retain previous value
        byte_q[3:0] <= nib_lo ^ 4'hf;
      end
    end
  end

  assign q_byte = byte_q;
  assign wide_q = wide_reg;
  assign rdata  = mem[rptr].valid ? mem[rptr].data : 32'hDEAD_BEEF;
  assign rtag   = mem[rptr].tag;
endmodule

module tb_partial_mem;
  logic        clk = 0, rst_n = 0, we = 0;
  logic [1:0]  wptr = 0, rptr = 0;
  logic [3:0]  nib_lo = 0, nib_hi = 0, wtag = 0, rtag;
  logic [31:0] wdata = 0, rdata;
  logic [7:0]  q_byte;
  logic [95:0] wide_q;

  partial_mem_unit dut (.*);

  initial begin
    #5 clk = 1; #5 clk = 0;
    rst_n = 1;
    we = 1;
    for (int i = 0; i < 4; i++) begin
      wptr   = i[1:0];
      rptr   = i[1:0];
      nib_lo = 4'(i + 1);
      nib_hi = 4'(i + 9);
      wdata  = 32'h1000_0000 + i;
      wtag   = 4'(i + 3);
      #5 clk = 1; #5 clk = 0;
      #1;
      $display("WRITE i=%0d q_byte=%h wide=%h rdata=%h rtag=%h",
               i, q_byte, wide_q, rdata, rtag);
    end
    we = 0;
    nib_lo = 4'h5;
    rptr = 2'd2;
    #5 clk = 1; #5 clk = 0;
    #1;
    $display("PASSED partial_mem q_byte=%h wide=%h rdata=%h rtag=%h",
             q_byte, wide_q, rdata, rtag);
    $finish;
  end
endmodule
EOF

nvc-sv -a partial_mem_nba.sv -e --rtl-islands=module tb_partial_mem
nvc-sv -r --wave=pmem_m3.fst tb_partial_mem > pmem_m3.out
grep -q "PASSED partial_mem" pmem_m3.out

nvc-sv -a partial_mem_nba.sv -e --rtl-islands=off tb_partial_mem
nvc-sv -r --wave=pmem_off.fst tb_partial_mem > pmem_off.out
diff -u pmem_off.out pmem_m3.out

# 4. 4-state X/Z clock edge transitions (IEEE 1800 Table 9-2)
cat >edge_4state.sv <<'EOF'
module edge_4state_unit (
  input  logic       clk_p,
  input  logic       clk_n,
  output logic [7:0] cnt_p,
  output logic [7:0] cnt_n
);
  logic [7:0] pq = 8'd0;
  logic [7:0] nq = 8'd0;

  always_ff @(posedge clk_p)
    pq <= pq + 8'd1;

  always_ff @(negedge clk_n)
    nq <= nq + 8'd1;

  assign cnt_p = pq;
  assign cnt_n = nq;
endmodule

module tb_edge_4state;
  logic clk_p = 1'b0, clk_n = 1'b1;
  logic [7:0] cnt_p, cnt_n;

  edge_4state_unit dut (.*);

  initial begin
    #1;
    // Posedge: 0->X (+1), X->Z (+0), Z->1 (+1), 1->Z (+0), Z->0 (+0), 0->Z (+1), Z->X (+0), X->1 (+1), 1->0 (+0), 0->1 (+1)
    #5 clk_p = 1'bx; #5 $display("P1=%0d", cnt_p);
    #5 clk_p = 1'bz; #5 $display("P2=%0d", cnt_p);
    #5 clk_p = 1'b1; #5 $display("P3=%0d", cnt_p);
    #5 clk_p = 1'bz; #5 $display("P4=%0d", cnt_p);
    #5 clk_p = 1'b0; #5 $display("P5=%0d", cnt_p);
    #5 clk_p = 1'bz; #5 $display("P6=%0d", cnt_p);
    #5 clk_p = 1'bx; #5 $display("P7=%0d", cnt_p);
    #5 clk_p = 1'b1; #5 $display("P8=%0d", cnt_p);
    #5 clk_p = 1'b0; #5 $display("P9=%0d", cnt_p);
    #5 clk_p = 1'b1; #5 $display("P10=%0d", cnt_p);

    // Negedge: 1->X (+1), X->Z (+0), Z->0 (+1), 0->Z (+0), Z->1 (+0), 1->Z (+1), Z->X (+0), X->0 (+1), 0->1 (+0), 1->0 (+1)
    #5 clk_n = 1'bx; #5 $display("N1=%0d", cnt_n);
    #5 clk_n = 1'bz; #5 $display("N2=%0d", cnt_n);
    #5 clk_n = 1'b0; #5 $display("N3=%0d", cnt_n);
    #5 clk_n = 1'bz; #5 $display("N4=%0d", cnt_n);
    #5 clk_n = 1'b1; #5 $display("N5=%0d", cnt_n);
    #5 clk_n = 1'bz; #5 $display("N6=%0d", cnt_n);
    #5 clk_n = 1'bx; #5 $display("N7=%0d", cnt_n);
    #5 clk_n = 1'b0; #5 $display("N8=%0d", cnt_n);
    #5 clk_n = 1'b1; #5 $display("N9=%0d", cnt_n);
    #5 clk_n = 1'b0; #5 $display("N10=%0d", cnt_n);

    if (cnt_p !== 8'd5 || cnt_n !== 8'd5) begin
      $display("FAILED edge_4state cnt_p=%0d cnt_n=%0d", cnt_p, cnt_n);
      $finish;
    end
    $display("PASSED edge_4state cnt_p=%0d cnt_n=%0d", cnt_p, cnt_n);
    $finish;
  end
endmodule
EOF

nvc-sv -a edge_4state.sv -e --rtl-islands=module tb_edge_4state
nvc-sv -r tb_edge_4state > edge_m3.out
grep -q "PASSED edge_4state" edge_m3.out

nvc-sv -a edge_4state.sv -e --rtl-islands=off tb_edge_4state
nvc-sv -r tb_edge_4state > edge_off.out
diff -u edge_off.out edge_m3.out

# 5. Multi-instance module with independent clocks/states
cat >multi_clk_inst.sv <<'EOF'
module ctr_unit #(
  parameter int STEP = 1
) (
  input  logic       clk,
  input  logic       rst_n,
  output logic [7:0] q
);
  logic [7:0] r;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      r <= 8'd0;
    else
      r <= r + 8'(STEP);
  end
  assign q = r;
endmodule

module tb_multi_clk;
  logic [2:0] clk = 3'b000;
  logic       rst_n = 1'b0;
  logic [7:0] q0, q1, q2;

  ctr_unit #(.STEP(1)) u0 (.clk(clk[0]), .rst_n(rst_n), .q(q0));
  ctr_unit #(.STEP(3)) u1 (.clk(clk[1]), .rst_n(rst_n), .q(q1));
  ctr_unit #(.STEP(5)) u2 (.clk(clk[2]), .rst_n(rst_n), .q(q2));

  initial begin
    #5 clk = 3'b111; #5 clk = 3'b000;
    rst_n = 1'b1;
    // Toggle each clock a different number of times
    repeat (4) begin
      #5 clk[0] = 1'b1; #5 clk[0] = 1'b0;
    end
    repeat (2) begin
      #5 clk[1] = 1'b1; #5 clk[1] = 1'b0;
    end
    #5 clk[2] = 1'b1; #5 clk[2] = 1'b0;
    #1;
    if (q0 !== 8'd4 || q1 !== 8'd6 || q2 !== 8'd5) begin
      $display("FAILED multi_clk q0=%0d q1=%0d q2=%0d", q0, q1, q2);
      $finish;
    end
    $display("PASSED multi_clk %0d %0d %0d", q0, q1, q2);
    $finish;
  end
endmodule
EOF

nvc-sv -a multi_clk_inst.sv -e --rtl-islands=module tb_multi_clk
nvc-sv -r tb_multi_clk > mclk_m3.out
grep -q "PASSED multi_clk" mclk_m3.out
nvc-sv -a multi_clk_inst.sv -e --rtl-islands=off tb_multi_clk
nvc-sv -r tb_multi_clk > mclk_off.out
diff -u mclk_off.out mclk_m3.out

# 6. Mixed VHDL + SV boundary with fused FF + comb island
cat >mixed_m3_sv.sv <<'EOF'
module sv_pipe_ff (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] a,
  input  logic [7:0] b,
  output logic [7:0] y
);
  logic [7:0] sum_c, q1, q2;

  always_comb
    sum_c = a + b;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      q1 <= 8'h00;
      q2 <= 8'h00;
    end else begin
      q1 <= sum_c;
      q2 <= q1 ^ 8'h33;
    end
  end

  assign y = q2;
endmodule
EOF

cat >mixed_m3_top.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixed_m3_top is
end entity;

architecture sim of mixed_m3_top is
  component sv_pipe_ff is
    port (
      clk   : in  std_logic;
      rst_n : in  std_logic;
      a     : in  std_logic_vector(7 downto 0);
      b     : in  std_logic_vector(7 downto 0);
      y     : out std_logic_vector(7 downto 0)
    );
  end component;

  signal clk   : std_logic := '0';
  signal rst_n : std_logic := '0';
  signal a, b, y : std_logic_vector(7 downto 0) := (others => '0');
begin
  clk <= not clk after 5 ns when now < 100 ns else '0';

  dut : sv_pipe_ff port map (clk => clk, rst_n => rst_n, a => a, b => b, y => y);

  stim : process
  begin
    wait until rising_edge(clk);
    wait for 1 ns;
    assert y = x"00" report "expected y=00 in reset" severity failure;

    wait until falling_edge(clk);
    rst_n <= '1';
    a <= x"12";
    b <= x"34";
    wait until rising_edge(clk); -- q1 <= 46, q2 <= 00^33 = 33
    wait for 1 ns;
    assert y = x"33" report "expected y=33 after cycle 1" severity failure;

    wait until falling_edge(clk);
    a <= x"01";
    b <= x"02";
    wait until rising_edge(clk); -- q1 <= 03, q2 <= 46^33 = 75
    wait for 1 ns;
    assert y = x"75" report "expected y=75 after cycle 2" severity failure;

    report "PASSED mixed_m3_top";
    wait;
  end process;
end architecture;
EOF

nvc-sv --std=2008 -a mixed_m3_sv.sv mixed_m3_top.vhd -e --rtl-islands=module mixed_m3_top
nvc-sv --std=2008 -r mixed_m3_top 2>&1 | tee mixed_m3.out
grep -q "PASSED mixed_m3_top" mixed_m3.out
