set -xe

# Verify M3 per-signal waveform fallback on otherwise-pruned VHDL and SV
# island outputs under --island-fast vs --no-island-fast vs NVC_RTL_ISLANDS=off.

cat >wave_vhdl.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity wave_dut is
    port (
        clk         : in  std_logic;
        din         : in  unsigned(7 downto 0);
        obs_out     : out unsigned(7 downto 0);
        pruned_comb : out unsigned(7 downto 0);
        pruned_ff   : out unsigned(7 downto 0)
    );
end entity;

architecture rtl of wave_dut is
    signal q_reg : unsigned(7 downto 0) := x"10";
begin
    p_comb : process (din, q_reg) is
    begin
        pruned_comb <= din xor x"a5";
        obs_out     <= q_reg + din;
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q_reg     <= din + x"03";
            pruned_ff <= (din + x"07") xor q_reg;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity wave_vhdl_tb is
end entity;

architecture test of wave_vhdl_tb is
    signal clk     : std_logic := '0';
    signal din     : unsigned(7 downto 0) := x"01";
    signal obs_out : unsigned(7 downto 0);
begin
    dut : entity work.wave_dut
        port map (
            clk         => clk,
            din         => din,
            obs_out     => obs_out,
            pruned_comb => open,
            pruned_ff   => open
        );

    p_stim : process is
    begin
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        din <= x"12";
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        din <= x"34";
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        din <= x"01";
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        wait for 5 ns;
        assert obs_out = x"05" report "unexpected obs_out";
        wait;
    end process;
end architecture;
EOF

cat >wave_sv.sv <<'EOF'
`timescale 1ns/1ns

module wave_sv_dut (
   input  logic       clk,
   input  logic [7:0] din,
   output logic [7:0] obs_out,
   output logic [7:0] pruned_comb,
   output logic [7:0] pruned_ff
);
   logic [7:0] q_reg = 8'h10;

   always_comb begin
      pruned_comb = din ^ 8'h5a;
      obs_out     = q_reg + din;
   end

   always_ff @(posedge clk) begin
      q_reg     <= din + 8'h03;
      pruned_ff <= (din + 8'h07) ^ q_reg;
   end
endmodule

module wave_sv_tb;
   logic       clk = 1'b0;
   logic [7:0] din = 8'h01;
   logic [7:0] obs_out;

   wave_sv_dut u_dut (
      .clk         (clk),
      .din         (din),
      .obs_out     (obs_out),
      .pruned_comb (),
      .pruned_ff   ()
   );

   initial begin
      #5 clk = 1'b1; #5 clk = 1'b0;
      din = 8'h12;
      #5 clk = 1'b1; #5 clk = 1'b0;
      din = 8'h34;
      #5 clk = 1'b1; #5 clk = 1'b0;
      din = 8'h01;
      #5 clk = 1'b1; #5 clk = 1'b0;
      #5;
      if (obs_out !== 8'h05) begin
         $display("FAILED obs_out=%h", obs_out);
         $finish(1);
      end
      $finish;
   end
endmodule
EOF

strip_date() {
   sed '/^\$date/,/^\$end/d' "$1" > "$2"
}

# --- VHDL wave fallback ---
NVC_RTL_ISLANDS=hier nvc --std=2008 -a wave_vhdl.vhd -e wave_vhdl_tb
NVC_RTL_ISLANDS=hier NVC_DUMP_ISLANDS=1 nvc --std=2008 -r --island-fast --wave=vhdl_fast.vcd --format=vcd wave_vhdl_tb 2>vhdl_dump.log
grep -q 'ISLAND_FAST: fast=1' vhdl_dump.log
grep -qE 'pruned=[1-9]' vhdl_dump.log
NVC_RTL_ISLANDS=hier nvc --std=2008 -r --no-island-fast --wave=vhdl_nofast.vcd --format=vcd wave_vhdl_tb
NVC_RTL_ISLANDS=off nvc --std=2008 -a wave_vhdl.vhd -e wave_vhdl_tb
NVC_RTL_ISLANDS=off nvc --std=2008 -r --no-island-fast --wave=vhdl_off.vcd --format=vcd wave_vhdl_tb

strip_date vhdl_fast.vcd   vhdl_fast.norm
strip_date vhdl_nofast.vcd vhdl_nofast.norm
strip_date vhdl_off.vcd    vhdl_off.norm
diff -u vhdl_nofast.norm vhdl_fast.norm
diff -u vhdl_off.norm    vhdl_fast.norm
grep -q 'pruned_comb' vhdl_fast.norm
grep -q 'pruned_ff'   vhdl_fast.norm
# din=0x12 ^ 0xa5 = 0xb7 = 10110111, din=0x34 ^ 0xa5 = 0x91 = 10010001
grep -q 'b10110111 ' vhdl_fast.norm
grep -q 'b10010001 ' vhdl_fast.norm

# --- SV wave fallback ---
NVC_RTL_ISLANDS=hier nvc -a wave_sv.sv -e wave_sv_tb
NVC_RTL_ISLANDS=hier NVC_DUMP_ISLANDS=1 nvc -r --island-fast --wave=sv_fast.vcd --format=vcd wave_sv_tb 2>sv_dump.log
grep -q 'ISLAND_FAST: fast=1' sv_dump.log
grep -qE 'pruned=[1-9]' sv_dump.log
NVC_RTL_ISLANDS=hier nvc -r --no-island-fast --wave=sv_nofast.vcd --format=vcd wave_sv_tb
NVC_RTL_ISLANDS=off nvc -a wave_sv.sv -e wave_sv_tb
NVC_RTL_ISLANDS=off nvc -r --no-island-fast --wave=sv_off.vcd --format=vcd wave_sv_tb

strip_date sv_fast.vcd   sv_fast.norm
strip_date sv_nofast.vcd sv_nofast.norm
strip_date sv_off.vcd    sv_off.norm
diff -u sv_nofast.norm sv_fast.norm
diff -u sv_off.norm    sv_fast.norm
grep -q 'pruned_comb' sv_fast.norm
grep -q 'pruned_ff'   sv_fast.norm
# din=0x12 ^ 0x5a = 0x48 = 01001000, din=0x34 ^ 0x5a = 0x6e = 01101110
grep -q 'b01001000 ' sv_fast.norm
grep -q 'b01101110 ' sv_fast.norm
