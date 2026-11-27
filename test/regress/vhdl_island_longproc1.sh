set -xe

# A report in a process fused into an RTL island names the process from the
# location of the statement.  A process longer than 255 lines must still be
# named, as with --rtl-islands=off.

{
    cat <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity longproc1 is
end entity;

architecture test of longproc1 is
    signal clk : std_logic := '0';
    signal n, m : unsigned(7 downto 0) := (others => '0');
begin
    p_short : process (clk) is
    begin
        if rising_edge(clk) then
            m <= n;
            report "short m=" & to_string(to_integer(m));
        end if;
    end process;

    p_long : process (clk) is
    begin
        if rising_edge(clk) then
            n <= n + 1;
VHDL_EOF
    for i in $(seq 300); do
        echo "            null;"
    done
    cat <<'VHDL_EOF'
            report "long n=" & to_string(to_integer(n));
        end if;
    end process;

    p_stim : process is
    begin
        for i in 0 to 3 loop
            wait for 1 ns;
            clk <= '1';
            wait for 1 ns;
            clk <= '0';
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF
} > longproc1.vhd

nvc --std=2008 --work=w_off -a longproc1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e longproc1 -r > off.log 2>&1
grep -q "Process :longproc1:p_long at" off.log

nvc --std=2008 --work=w_b -a longproc1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e longproc1 \
    > b_dump.log
grep -q 'LONGPROC1 islands=1 fused_procs=2/3' b_dump.log
nvc --std=2008 --work=w_b -r longproc1 > b.log 2>&1
sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
diff -u off.log b.log
