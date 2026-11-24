set -xe

# 1. Pure assign chain + mixed always_comb/always @(*) DAG + X/Z propagation
cat >dag_4state.sv <<'EOF'
package m2_pkg;
  function automatic logic [7:0] rot_xor(input logic [7:0] x, input logic [7:0] k);
    logic [7:0] t;
    t = {x[6:0], x[7]};
    return t ^ k;
  endfunction
endpackage

module dag_4state (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] in_a,
  input  bit   [7:0] in_b,
  input  logic       sel,
  output logic [7:0] out_y,
  output bit   [7:0] out_b,
  output logic [3:0] out_nib
);
  import m2_pkg::*;

  logic [7:0] stage1, stage2, stage3, q_reg;
  wire  [7:0] z_wire;

  assign stage1 = in_a + {4'b0, in_b[3:0]};

  always_comb begin
    logic [7:0] tmp;
    tmp = rot_xor(stage1, q_reg);
    if (sel)
      stage2 = tmp;
    else
      stage2 = stage1 ^ 8'h3c;
  end

  always @(*) begin
    stage3 = stage2 + {4'h0, in_b[7:4]};
  end

  assign out_y      = {stage3[7:4], z_wire[3:0]};
  assign z_wire     = sel ? stage3 : 8'bzzzz_zzzz;
  assign out_b      = stage3;
  assign out_nib    = stage3[3:0];

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      q_reg <= 8'h5a;
    else
      q_reg <= stage3;
  end
endmodule

module tb_dag_4state;
  logic       clk = 0, rst_n = 1, sel = 1;
  logic [7:0] in_a = 8'h11, out_y;
  bit   [7:0] in_b = 8'h23, out_b;
  logic [3:0] out_nib;

  dag_4state dut (.*);

  initial begin
    #1;
    // Before reset: q_reg is X -> stage2/stage3/out_y are X, out_b (2-state) is 0
    if (out_y !== 8'hxx || out_b !== 8'h00) begin
      $display("FAILED init out_y=%b out_b=%h", out_y, out_b);
      $finish;
    end

    #4 rst_n = 0; #5 clk = 1; #5 clk = 0; // Reset: q_reg <= 8'h5a
    #1;
    if (out_y !== 8'h74 || out_nib !== 4'h4) begin
      $display("FAILED post-reset out_y=%h out_nib=%h", out_y, out_nib);
      $finish;
    end

    // Z propagation when sel=0
    sel = 0;
    in_a = 8'h05;
    #1;
    if (out_y !== 8'b0011_zzzz) begin
      $display("FAILED z-prop out_y=%b", out_y);
      $finish;
    end

    // X propagation into 4-state vs 2-state clamping
    sel = 1;
    in_a = 8'hxx;
    #1;
    if (out_y !== 8'bxxxx_xxxx || out_b !== 8'h00) begin
      $display("FAILED x-clamp out_y=%b out_b=%b", out_y, out_b);
      $finish;
    end

    // Known stimulus after rst_n=1
    rst_n = 1;
    in_a = 8'h10;
    in_b = 8'h42;
    #4 clk = 1; #5 clk = 0;
    #1;
    $display("PASSED dag_4state out_y=%h out_b=%h", out_y, out_b);
    $finish;
  end
endmodule
EOF

nvc-sv -a dag_4state.sv -e --rtl-islands=module tb_dag_4state
nvc-sv -r --wave=dag_m2.fst tb_dag_4state > dag_m2.out
grep -q "PASSED dag_4state" dag_m2.out

nvc-sv -a dag_4state.sv -e --rtl-islands=off tb_dag_4state
nvc-sv -r --wave=dag_off.fst tb_dag_4state > dag_off.out
diff -u dag_off.out dag_m2.out

# 2. Multi-instance parameterized module (4x genvar + packed arrays + structs)
cat >multi_inst.sv <<'EOF'
module lane_unit #(
  parameter int ID = 0
) (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] din,
  output logic [7:0] dout
);
  logic [7:0] mem [0:3];
  logic [1:0] idx_q;
  logic [7:0] xform, tagged_val;

  assign xform = mem[idx_q] ^ 8'(ID * 17);

  always_comb begin
    logic [7:0] acc;
    acc = xform + din;
    for (int i = 0; i < 4; i++)
      acc ^= {6'b0, mem[i][1:0]};
    tagged_val = acc;
  end

  assign dout = tagged_val;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      idx_q <= 2'd0;
      for (int i = 0; i < 4; i++)
        mem[i] <= 8'(ID + i);
    end else begin
      mem[idx_q] <= din;
      idx_q <= idx_q + 2'd1;
    end
  end
endmodule

module tb_multi_inst;
  logic clk = 0, rst_n = 0;
  logic [3:0][7:0] din_v, dout_v;

  for (genvar g = 0; g < 4; g++) begin : g_lane
    lane_unit #(.ID(g)) u_lane (
      .clk   (clk),
      .rst_n (rst_n),
      .din   (din_v[g]),
      .dout  (dout_v[g])
    );
  end

  initial begin
    for (int g = 0; g < 4; g++)
      din_v[g] = 8'(8'h10 + g);
    #5 clk = 1; #5 clk = 0;
    rst_n = 1;
    repeat (6) begin
      for (int g = 0; g < 4; g++)
        din_v[g] = din_v[g] + 8'(g + 1);
      #5 clk = 1; #5 clk = 0;
    end
    #1;
    $display("PASSED multi_inst %h %h %h %h",
             dout_v[0], dout_v[1], dout_v[2], dout_v[3]);
    $finish;
  end
endmodule
EOF

nvc-sv -a multi_inst.sv -e --rtl-islands=module tb_multi_inst
nvc-sv -r tb_multi_inst > multi_m2.out
grep -q "PASSED multi_inst" multi_m2.out

nvc-sv -a multi_inst.sv -e --rtl-islands=off tb_multi_inst
nvc-sv -r tb_multi_inst > multi_off.out
diff -u multi_off.out multi_m2.out

# 3. Mixed VHDL + SV boundary with U/X/Z/0/1
cat >mixed_sv.sv <<'EOF'
module sv_sub (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] a,
  input  logic [7:0] b,
  output logic [7:0] y,
  output logic       valid_o
);
  logic [7:0] comb_sum;
  logic       valid_q;

  always_comb begin
    comb_sum = a ^ b;
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      valid_q <= 1'b0;
    else
      valid_q <= 1'b1;
  end

  assign y       = valid_q ? comb_sum : 8'h00;
  assign valid_o = valid_q;
endmodule
EOF

cat >mixed_top.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixed_m2_top is
end entity;

architecture sim of mixed_m2_top is
  component sv_sub is
    port (
      clk     : in  std_logic;
      rst_n   : in  std_logic;
      a       : in  std_logic_vector(7 downto 0);
      b       : in  std_logic_vector(7 downto 0);
      y       : out std_logic_vector(7 downto 0);
      valid_o : out std_logic
    );
  end component;

  signal clk     : std_logic := '0';
  signal rst_n   : std_logic := '0';
  signal a, b, y : std_logic_vector(7 downto 0) := (others => '0');
  signal valid_o : std_logic;
begin
  clk <= not clk after 5 ns when now < 100 ns else '0';

  dut : sv_sub
    port map (clk => clk, rst_n => rst_n, a => a, b => b, y => y, valid_o => valid_o);

  stim : process
  begin
    a <= (others => 'U');
    b <= (others => 'Z');
    wait until rising_edge(clk);
    wait for 1 ns;
    assert valid_o = '0' report "expected valid_o=0 during reset" severity failure;
    assert y = x"00" report "expected y=00 during reset" severity failure;

    wait until falling_edge(clk);
    rst_n <= '1';
    a <= x"A5";
    b <= x"3C";
    wait until rising_edge(clk);
    wait for 1 ns;
    assert valid_o = '1' report "expected valid_o=1 after reset" severity failure;
    assert y = x"99" report "expected y=99" severity failure;
    report "PASSED mixed_m2_top";
    wait;
  end process;
end architecture;
EOF

nvc-sv --std=2008 -a mixed_sv.sv mixed_top.vhd -e --rtl-islands=module mixed_m2_top
nvc-sv --std=2008 -r mixed_m2_top 2>&1 | tee mixed_m2.out
grep -q "PASSED mixed_m2_top" mixed_m2.out

# 4. Combinational SCC clean ejection
cat >scc_comb.sv <<'EOF'
module scc_comb (
  input  logic en,
  input  logic d,
  output logic y
);
  logic a, b;
  assign a = en ? b : d;
  assign b = a;
  assign y = ~a;
endmodule

module tb_scc_comb;
  logic en = 0, d = 1, y;
  scc_comb dut (.*);
  initial begin
    #1;
    if (y !== 1'b0) begin
      $display("FAILED scc_comb y=%b", y);
      $finish;
    end
    $display("PASSED scc_comb");
    $finish;
  end
endmodule
EOF

nvc-sv -a scc_comb.sv -e --rtl-islands=module tb_scc_comb -r > scc.out
grep -q "PASSED scc_comb" scc.out
