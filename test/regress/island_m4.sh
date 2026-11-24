set -xe

# 1. SVA concurrent assert/cover property + $past observing fused island FF & comb signals
cat >sva_island.sv <<'EOF'
module sva_dut (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] din,
  output logic [7:0] dout
);
  logic [7:0] stage_c;
  logic [7:0] q1, q2;

  always_comb
    stage_c = din + 8'h01;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      q1 <= 8'h00;
      q2 <= 8'h00;
    end else begin
      q1 <= stage_c;
      q2 <= q1;
    end
  end

  assign dout = q2;

  property p_pipe;
    @(posedge clk) disable iff (!rst_n)
    ($past(rst_n, 2) && $past(rst_n, 1)) |-> (q2 == $past(stage_c, 2));
  endproperty

  a_pipe: assert property (p_pipe) else $fatal(1, "SVA p_pipe failed");
  c_hit:  cover property (@(posedge clk) rst_n && (q1 == 8'h21))
    $display("COVER_HIT q1=%h q2=%h", q1, q2);
endmodule

module tb_sva;
  logic       clk = 0, rst_n = 1;
  logic [7:0] din = 8'h00, dout;

  sva_dut dut (.*);

  always #5 clk = ~clk;

  initial begin
    #2 rst_n = 0;
    #10 rst_n = 1;
    din = 8'h10;
    #10 din = 8'h20;
    #10 din = 8'h30;
    #10 din = 8'h40;
    #20;
    $display("PASSED sva_island dout=%h", dout);
    $finish;
  end
endmodule
EOF

nvc-sv -a sva_island.sv -e --rtl-islands=module tb_sva
nvc-sv -r --wave=sva_m4.fst tb_sva > sva_m4.out
grep -q "COVER_HIT" sva_m4.out
grep -q "PASSED sva_island" sva_m4.out

nvc-sv -a sva_island.sv -e --rtl-islands=off tb_sva
nvc-sv -r tb_sva > sva_off.out
diff -u sva_off.out sva_m4.out

# 2. VHDL external name reading, watching, forcing, and releasing fused SV island internals
cat >ename_island_dut.sv <<'EOF'
module ename_island_dut (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] din,
  output logic [7:0] dout
);
  logic [7:0] internal_comb;
  logic [7:0] internal_q;

  always_comb
    internal_comb = din ^ 8'h5a;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      internal_q <= 8'h00;
    else
      internal_q <= internal_comb;
  end

  assign dout = internal_q + 8'h01;
endmodule
EOF

cat >ename_island_tb.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;

entity ename_island_tb is
end entity;

architecture sim of ename_island_tb is
  component ename_island_dut is
    port (
      clk   : in  std_logic;
      rst_n : in  std_logic;
      din   : in  std_logic_vector(7 downto 0);
      dout  : out std_logic_vector(7 downto 0)
    );
  end component;

  signal clk   : std_logic := '0';
  signal rst_n : std_logic := '0';
  signal din   : std_logic_vector(7 downto 0) := x"11";
  signal dout  : std_logic_vector(7 downto 0);
  signal ev_q  : integer := 0;
  signal done  : boolean := false;
begin
  clk <= not clk after 5 ns when not done else '0';

  u_dut : ename_island_dut
    port map (clk => clk, rst_n => rst_n, din => din, dout => dout);

  p_watch : process is
    alias aq is << signal .ename_island_tb.u_dut.internal_q : std_logic_vector(7 downto 0) >>;
  begin
    wait on aq;
    assert aq'event;
    ev_q <= ev_q + 1;
  end process;

  p_main : process is
    alias ac is << signal .ename_island_tb.u_dut.internal_comb : std_logic_vector(7 downto 0) >>;
    alias aq is << signal .ename_island_tb.u_dut.internal_q : std_logic_vector(7 downto 0) >>;
  begin
    wait for 1 ns;
    assert ac = x"4b" report "ac init mismatch" severity failure;

    wait until falling_edge(clk);
    rst_n <= '1';
    din <= x"22";
    wait for 1 ns;
    assert ac = x"78" report "ac comb update mismatch" severity failure;

    wait until rising_edge(clk);
    wait for 1 ns;
    assert aq = x"78" report "aq ff update mismatch" severity failure;
    assert dout = x"79" report "dout mismatch" severity failure;
    assert ev_q >= 1 report "aq event watch did not fire" severity failure;

    -- Force and release internal_q via VHDL external name
    aq <= force x"a0";
    wait for 1 ns;
    assert aq = x"a0" report "aq force mismatch" severity failure;

    aq <= release;
    wait until falling_edge(clk);
    din <= x"33"; -- 0x33 ^ 0x5a = 0x69
    wait until rising_edge(clk);
    wait for 1 ns;
    assert aq = x"69" report "aq post-release mismatch" severity failure;
    assert dout = x"6a" report "dout post-release mismatch" severity failure;

    report "PASSED ename_island";
    done <= true;
    wait;
  end process;
end architecture;
EOF

nvc-sv --std=2008 -a ename_island_dut.sv ename_island_tb.vhd -e --rtl-islands=module ename_island_tb
nvc-sv --std=2008 -r ename_island_tb 2>&1 | tee ename_m4.out
grep -q "PASSED ename_island" ename_m4.out

nvc-sv --std=2008 -a ename_island_dut.sv ename_island_tb.vhd -e --rtl-islands=off ename_island_tb
nvc-sv --std=2008 -r ename_island_tb > ename_off.out 2>&1
diff -u ename_off.out ename_m4.out

# 3. SV hierarchical ref reading & watching island internals + FST internal dump diff
cat >hier_island.sv <<'EOF'
module hier_dut (
  input  logic       clk,
  input  logic [7:0] d,
  output logic [7:0] y
);
  logic [7:0] c1, q1;
  always_comb c1 = d + 8'h03;
  always_ff @(posedge clk) q1 <= c1;
  assign y = q1 ^ 8'h0f;
endmodule

module tb_hier_island;
  logic clk = 0;
  logic [7:0] d = 8'h10, y;
  int seen = 0;

  hier_dut dut (.*);

  always @(dut.q1)
    seen++;

  initial begin
    #1;
    if (dut.c1 !== 8'h13) $fatal(1, "c1=%h", dut.c1);
    #4 clk = 1; #5 clk = 0;
    #1;
    if (dut.q1 !== 8'h13 || y !== 8'h1c || seen < 1)
      $fatal(1, "q1=%h y=%h seen=%0d", dut.q1, y, seen);
    d = 8'h20;
    #4 clk = 1; #5 clk = 0;
    #1;
    if (dut.q1 !== 8'h23 || y !== 8'h2c || seen < 2)
      $fatal(1, "q1=%h y=%h seen=%0d", dut.q1, y, seen);
    $display("PASSED hier_island q1=%h y=%h", dut.q1, y);
    $finish;
  end
endmodule
EOF

nvc-sv -a hier_island.sv -e --rtl-islands=module tb_hier_island
nvc-sv -r --wave=hier_m4.fst tb_hier_island > hier_m4.out
grep -q "PASSED hier_island" hier_m4.out

nvc-sv -a hier_island.sv -e --rtl-islands=off tb_hier_island
nvc-sv -r --wave=hier_off.fst tb_hier_island > hier_off.out
diff -u hier_off.out hier_m4.out
fst2vcd -f hier_m4.fst > hier_m4.vcd
fst2vcd -f hier_off.fst > hier_off.vcd
diff -u hier_off.vcd hier_m4.vcd
