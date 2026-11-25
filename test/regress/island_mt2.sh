#!/bin/bash -ex
set -xe

if [ -n "${ASAN_OPTIONS:-}" ]; then
  export ASAN_OPTIONS="$ASAN_OPTIONS:detect_leaks=0"
fi

# --- Subtest 1: SV disjoint multi-driver (struct fields, slices, unpacked) + pkg helper ---
cat > sv_mt2.sv <<'EOF'
package sv_mt2_pkg;
  typedef struct packed {
    logic [3:0] hdr;
    logic [3:0] tag;
    logic [7:0] payload;
  } pkt_t;

  function automatic logic [7:0] mix8(input logic [7:0] a, input logic [7:0] b);
    return (a ^ b) + 8'h13;
  endfunction
endpackage

module sv_mt2_unit #(parameter int ID = 0) (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [7:0] din,
  output logic [15:0] pkt_out,
  output logic [15:0] bus_out,
  output logic [7:0]  arr_sum
);
  import sv_mt2_pkg::*;

  pkt_t        pkt_d, pkt_q;
  logic [15:0] bus_d, bus_q;
  logic [7:0]  lanes [0:1];

  // Disjoint constant packed struct field writes
  assign pkt_d.hdr     = din[3:0] ^ ID[3:0];
  assign pkt_d.tag     = din[7:4] + 4'h3;
  assign pkt_d.payload = mix8(din, pkt_q.payload);

  // Disjoint constant packed vector slice writes
  assign bus_d[7:0]  = din + ID[7:0];
  assign bus_d[15:8] = pkt_q.payload ^ bus_q[7:0];

  // Disjoint constant unpacked array element writes
  assign lanes[0] = bus_q[7:0] ^ 8'h5a;
  assign lanes[1] = bus_q[15:8] + 8'h07;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      pkt_q <= '0;
      bus_q <= 16'h0000;
    end else begin
      pkt_q <= pkt_d;
      bus_q <= bus_d;
    end
  end

  assign pkt_out = pkt_q;
  assign bus_out = bus_q;
  assign arr_sum = lanes[0] + lanes[1];
endmodule

module tb_sv_mt2;
  localparam int N = 16;
  logic       clk = 1'b0, rst_n = 1'b0;
  logic [7:0] din [0:N-1];
  logic [15:0] pkt_out [0:N-1];
  logic [15:0] bus_out [0:N-1];
  logic [7:0]  arr_sum [0:N-1];

  for (genvar i = 0; i < N; i++) begin : g_u
    sv_mt2_unit #(.ID(i + 1)) u (
      .clk(clk), .rst_n(rst_n), .din(din[i]),
      .pkt_out(pkt_out[i]), .bus_out(bus_out[i]), .arr_sum(arr_sum[i])
    );
  end

  always #5 clk = ~clk;

  initial begin
    for (int i = 0; i < N; i++) din[i] = (i * 11 + 5) & 8'hff;
    #12 rst_n = 1'b1;
    for (int cyc = 0; cyc < 20; cyc++) begin
      @(negedge clk);
      for (int i = 0; i < N; i++) din[i] = ((cyc * 17 + i * 7 + 3) & 8'hff);
      @(posedge clk);
      #1;
      if ((cyc % 4) == 0)
        $display("SV cyc=%0d p0=%04h b0=%04h a0=%02h p15=%04h b15=%04h a15=%02h",
                 cyc, pkt_out[0], bus_out[0], arr_sum[0],
                 pkt_out[N-1], bus_out[N-1], arr_sum[N-1]);
    end
    $finish;
  end
endmodule
EOF

nvc --work=work_sv -a sv_mt2.sv
nvc --work=work_sv --rtl-islands=off -e tb_sv_mt2
nvc --work=work_sv -r --format=fst --wave=sv_off.fst tb_sv_mt2 > sv_off.out
nvc --work=work_sv --rtl-islands=hier --dump-islands -e tb_sv_mt2 > sv_elab.log 2>&1
grep -qE "ISLAND_MODULE .*sv_mt2_unit.*islands=[1-9]" sv_elab.log
! grep -q "multi_driver_target=" sv_elab.log
nvc --work=work_sv --threads=1 -r --format=fst --wave=sv_hier.fst tb_sv_mt2 > sv_h1.out
nvc --work=work_sv --threads=4 -r tb_sv_mt2 > sv_h4.out
diff -u sv_off.out sv_h1.out
diff -u sv_off.out sv_h4.out

nvc --work=work_sv --rtl-islands=hier --2state=islands -e tb_sv_mt2
nvc --work=work_sv --threads=4 -r tb_sv_mt2 > sv_2s4.out
diff -u sv_off.out sv_2s4.out

if command -v fst2vcd >/dev/null 2>&1; then
  fst2vcd sv_off.fst | grep -v '^\$date' > sv_off.vcd
  fst2vcd sv_hier.fst | grep -v '^\$date' > sv_hier.vcd
  diff -u sv_off.vcd sv_hier.vcd
fi

# --- Subtest 2: VHDL enum/integer/record/constant-slice/generic islands + FST parity ---
cat > vhdl_mt2.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package vhdl_mt2_pkg is
  type fsm_t is (S_IDLE, S_RUN, S_HOLD, S_DONE);
  type rec_t is record
    st  : fsm_t;
    cnt : integer range 0 to 15;
    val : std_ulogic_vector(7 downto 0);
  end record;
  constant REC_INIT : rec_t := (st => S_IDLE, cnt => 0, val => x"00");
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.vhdl_mt2_pkg.all;

entity vhdl_mt2_unit is
  generic (
    SEED : std_ulogic_vector(7 downto 0) := x"15"
  );
  port (
    clk   : in  std_ulogic;
    rst   : in  std_ulogic;
    en    : in  std_ulogic;
    din   : in  std_ulogic_vector(7 downto 0);
    dout  : out std_ulogic_vector(7 downto 0);
    split : out std_ulogic_vector(7 downto 0)
  );
end entity;

architecture rtl of vhdl_mt2_unit is
  signal r, r_next : rec_t := REC_INIT;
  signal bus_s     : std_ulogic_vector(7 downto 0) := (others => '0');
begin
  -- Disjoint constant-slice concurrent drivers
  bus_s(3 downto 0) <= r.val(3 downto 0) xor SEED(3 downto 0);
  bus_s(7 downto 4) <= din(7 downto 4) xor r.val(7 downto 4);

  comb : process(all)
    variable v : rec_t;
  begin
    v := r;
    case r.st is
      when S_IDLE =>
        if en = '1' then
          v.st  := S_RUN;
          v.cnt := 1;
          v.val := din xor SEED;
        end if;
      when S_RUN =>
        if r.cnt = 15 then
          v.cnt := 0;
          v.st  := S_HOLD;
        else
          v.cnt := r.cnt + 1;
        end if;
        v.val := std_ulogic_vector(unsigned(r.val) + unsigned(din));
      when S_HOLD =>
        v.st  := S_DONE;
        v.val := r.val xor x"A5";
      when S_DONE =>
        v.st  := S_IDLE;
        v.cnt := 0;
        v.val := r.val xor SEED;
    end case;
    r_next <= v;
  end process;

  regs : process(clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        r <= REC_INIT;
      else
        r <= r_next;
      end if;
    end if;
  end process;

  dout  <= r.val;
  split <= bus_s;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.env.all;

entity tb_vhdl_mt2 is
end entity;

architecture sim of tb_vhdl_mt2 is
  signal clk, rst, en : std_ulogic := '0';
  signal din, d0, s0, d1, s1 : std_ulogic_vector(7 downto 0) := (others => '0');
begin
  u0 : entity work.vhdl_mt2_unit
    generic map (SEED => x"23")
    port map (clk => clk, rst => rst, en => en, din => din, dout => d0, split => s0);
  u1 : entity work.vhdl_mt2_unit
    generic map (SEED => x"7C")
    port map (clk => clk, rst => rst, en => en, din => d0, dout => d1, split => s1);

  stim : process
  begin
    rst <= '1';
    en  <= '0';
    for cyc in 0 to 29 loop
      if cyc = 2 then
        rst <= '0';
        en  <= '1';
      end if;
      din <= std_ulogic_vector(to_unsigned((cyc * 13 + 7) mod 256, 8));
      clk <= '0'; wait for 5 ns;
      clk <= '1'; wait for 5 ns;
      if (cyc mod 5) = 0 then
        report "VHDL cyc=" & integer'image(cyc)
               & " d0=" & to_hstring(d0) & " s0=" & to_hstring(s0)
               & " d1=" & to_hstring(d1) & " s1=" & to_hstring(s1);
      end if;
    end loop;
    finish;
  end process;
end architecture;
EOF

nvc --std=2008 --work=work_vhdl -a vhdl_mt2.vhd
nvc --std=2008 --work=work_vhdl --rtl-islands=off -e tb_vhdl_mt2
nvc --std=2008 --work=work_vhdl -r --format=fst --wave=vhdl_off.fst tb_vhdl_mt2 > vhdl_off.out 2>&1
nvc --std=2008 --work=work_vhdl --rtl-islands=hier --dump-islands -e tb_vhdl_mt2 > vhdl_elab.log 2>&1
grep -qE "VHDL_ISLAND_ELAB: .*islands=[1-9]" vhdl_elab.log
nvc --std=2008 --work=work_vhdl --threads=1 -r --format=fst --wave=vhdl_hier.fst tb_vhdl_mt2 > vhdl_h1.out 2>&1
nvc --std=2008 --work=work_vhdl --threads=4 -r tb_vhdl_mt2 > vhdl_h4.out 2>&1
diff -u vhdl_off.out vhdl_h1.out
diff -u vhdl_off.out vhdl_h4.out

nvc --std=2008 --work=work_vhdl --rtl-islands=hier --2state=islands -e tb_vhdl_mt2
nvc --std=2008 --work=work_vhdl --threads=4 -r tb_vhdl_mt2 > vhdl_2s4.out 2>&1
diff -u vhdl_off.out vhdl_2s4.out

if command -v fst2vcd >/dev/null 2>&1; then
  fst2vcd vhdl_off.fst | grep -v '^\$date' > vhdl_off.vcd
  fst2vcd vhdl_hier.fst | grep -v '^\$date' > vhdl_hier.vcd
  diff -u vhdl_off.vcd vhdl_hier.vcd
fi

# --- Subtest 3: Parallel read-only SVA under --threads=1,2,4 ---
cat > sva_mt2.sv <<'EOF'
module tb_sva_mt2;
  localparam int NP = 24;
  logic clk = 1'b0;
  logic rst_n = 1'b0;
  logic [NP-1:0] req = '0;
  logic [NP-1:0] ack = '0;
  int pass_cnt [0:NP-1];
  int fail_cnt [0:NP-1];
  int cov_cnt  [0:NP-1];
  int cyc = 0;

  always #5 clk = ~clk;

  for (genvar i = 0; i < NP; i++) begin : g_p
    assert property (@(posedge clk) disable iff (!rst_n)
                     req[i] |-> ##[1:3] ack[i])
      begin
        pass_cnt[i]++;
      end
      else begin
        fail_cnt[i]++;
        $display("FAIL p=%0d cyc=%0d", i, cyc);
      end

    cover property (@(posedge clk) disable iff (!rst_n)
                    req[i] ##[1:3] ack[i])
      begin
        cov_cnt[i]++;
      end
  end

  initial begin
    for (int i = 0; i < NP; i++) begin
      pass_cnt[i] = 0;
      fail_cnt[i] = 0;
      cov_cnt[i]  = 0;
    end
    #12 rst_n = 1'b1;
    for (int t = 0; t < 160; t++) begin
      @(negedge clk);
      cyc = t;
      for (int i = 0; i < NP; i++) begin
        // Trigger overlapping multi-cycle sequences; periodically drop ack so
        // multiple assertions fail on the same clock edge in deterministic order
        req[i] = (((t + i) % 5) == 0);
        if ((t % 29) == 0 || (t % 29) == 1 || (t % 29) == 2)
          ack[i] = 1'b0;
        else
          ack[i] = (((t + i) % 5) == 2);
      end
      @(posedge clk);
    end
    @(negedge clk);
    req = '0;
    ack = '0;
    repeat (4) @(posedge clk);
    #1;
    for (int i = 0; i < NP; i++)
      $display("SUM p=%0d pass=%0d fail=%0d cov=%0d",
               i, pass_cnt[i], fail_cnt[i], cov_cnt[i]);
    $finish;
  end
endmodule
EOF

nvc --work=work_sva -a sva_mt2.sv
nvc --work=work_sva --rtl-islands=off -e tb_sva_mt2
nvc --work=work_sva --threads=1 -r tb_sva_mt2 > sva_off_t1.out
nvc --work=work_sva --threads=2 -r tb_sva_mt2 > sva_off_t2.out
nvc --work=work_sva --threads=4 -r tb_sva_mt2 > sva_off_t4.out
grep -q "^FAIL p=0 " sva_off_t1.out
diff -u sva_off_t1.out sva_off_t2.out
diff -u sva_off_t1.out sva_off_t4.out

nvc --work=work_sva --rtl-islands=hier -e tb_sva_mt2
nvc --work=work_sva --threads=1 -r tb_sva_mt2 > sva_hier_t1.out
nvc --work=work_sva --threads=4 -r tb_sva_mt2 > sva_hier_t4.out
diff -u sva_off_t1.out sva_hier_t1.out
diff -u sva_off_t1.out sva_hier_t4.out
