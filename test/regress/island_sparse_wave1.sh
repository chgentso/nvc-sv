set -xe

# Verify M3 waveform fallback on sparse-row VHDL RAMs (> 1024 bits)
# under --island-fast vs --no-island-fast vs NVC_RTL_ISLANDS=off.

cat >sparse_wave.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sparse_wave_dut is
    port (
        clk     : in  std_ulogic;
        we      : in  std_ulogic;
        ben     : in  std_ulogic_vector(3 downto 0);
        waddr   : in  unsigned(5 downto 0);
        wdata   : in  std_ulogic_vector(31 downto 0);
        raddr   : in  unsigned(5 downto 0);
        s_rdata : out std_ulogic_vector(31 downto 0);
        a_rdata : out std_ulogic_vector(31 downto 0)
    );
end entity;

architecture rtl of sparse_wave_dut is
    type ram_t is array (0 to 63) of std_ulogic_vector(31 downto 0);
    signal mem : ram_t := (others => (others => '0'));
begin
    p_ram : process (clk) is
    begin
        if rising_edge(clk) then
            if we = '1' then
                for b in 0 to 3 loop
                    if ben(b) = '1' then
                        mem(to_integer(waddr))((b + 1) * 8 - 1 downto b * 8) <=
                            wdata((b + 1) * 8 - 1 downto b * 8);
                    end if;
                end loop;
            end if;
            s_rdata <= mem(to_integer(raddr));
        end if;
    end process;

    a_rdata <= mem(to_integer(raddr));
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sparse_wave_tb is
end entity;

architecture test of sparse_wave_tb is
    signal clk     : std_ulogic := '0';
    signal we      : std_ulogic := '0';
    signal ben     : std_ulogic_vector(3 downto 0) := "1111";
    signal waddr   : unsigned(5 downto 0) := (others => '0');
    signal wdata   : std_ulogic_vector(31 downto 0) := (others => '0');
    signal raddr   : unsigned(5 downto 0) := (others => '0');
    signal s_rdata : std_ulogic_vector(31 downto 0);
    signal a_rdata : std_ulogic_vector(31 downto 0);
begin
    dut : entity work.sparse_wave_dut
        port map (
            clk     => clk,
            we      => we,
            ben     => ben,
            waddr   => waddr,
            wdata   => wdata,
            raddr   => raddr,
            s_rdata => s_rdata,
            a_rdata => a_rdata
        );

    p_stim : process is
    begin
        we <= '1'; ben <= "1111"; waddr <= to_unsigned(3, 6); wdata <= x"12345678"; raddr <= to_unsigned(3, 6);
        wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
        ben <= "0101"; wdata <= x"00AA00BB";
        wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
        waddr <= to_unsigned(19, 6); ben <= "1111"; wdata <= x"DEADBEEF";
        wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
        we <= '0'; raddr <= to_unsigned(19, 6);
        wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
        assert s_rdata = x"DEADBEEF" report "s_rdata mismatch";
        assert a_rdata = x"DEADBEEF" report "a_rdata mismatch";
        wait;
    end process;
end architecture;
EOF

strip_date() {
   sed '/^\$date/,/^\$end/d' "$1" > "$2"
}

NVC_RTL_ISLANDS=hier NVC_DUMP_ISLANDS=1 nvc --std=2008 -a sparse_wave.vhd -e sparse_wave_tb >elab_dump.log
grep -q 'SPARSE_WAVE_TB.DUT islands=1 fused_procs=2/2' elab_dump.log
NVC_RTL_ISLANDS=hier nvc --std=2008 -r --island-fast --wave=fast.vcd --format=vcd sparse_wave_tb
NVC_RTL_ISLANDS=hier nvc --std=2008 -r --no-island-fast --wave=nofast.vcd --format=vcd sparse_wave_tb
NVC_RTL_ISLANDS=off nvc --std=2008 -a sparse_wave.vhd -e sparse_wave_tb
NVC_RTL_ISLANDS=off nvc --std=2008 -r --no-island-fast --wave=off.vcd --format=vcd sparse_wave_tb

strip_date fast.vcd   fast.norm
strip_date nofast.vcd nofast.norm
strip_date off.vcd    off.norm
diff -u nofast.norm fast.norm
diff -u off.norm    fast.norm
