set -xe

# Zero-delay assignments that mostly leave the driver value unchanged, in
# code compiled by LLVM, next to RTL islands evaluated by worker threads:
# the results do not depend on the number of threads or on islands

cat >mt44.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mt44 is
end entity;

architecture test of mt44 is
    type rec_t is record
        a : std_logic_vector(15 downto 0);
        b : std_logic;
        c : unsigned(7 downto 0);
        d : integer;
    end record;

    signal clk   : std_logic := '0';
    signal cnt   : unsigned(7 downto 0) := (others => '0');
    signal x, y  : unsigned(7 downto 0) := (others => '0');
    signal r     : rec_t := (X"0000", '0', X"00", 0);
    signal seen  : natural := 0;
begin
    clk <= not clk after 5 ns when now < 2 us;

    regs: process (clk) is
    begin
        if rising_edge(clk) then
            cnt <= cnt + 1;
        end if;
    end process;

    comb1: process (cnt) is
    begin
        x <= cnt xor X"5a";
    end process;

    comb2: process (x) is
    begin
        y <= x + 3;
    end process;

    -- Not fused because of the report: assigns every field each time,
    -- most of them with the value they already have
    outs: process (y) is
        variable v : rec_t;
    begin
        v := r;
        if y(3 downto 0) = X"0" then
            v.a := std_logic_vector(y) & std_logic_vector(y);
            v.d := to_integer(y);
            report "y=" & to_hstring(y);
        end if;
        v.b := y(7);
        v.c := y and X"f0";
        r <= v;
    end process;

    mon: process (r.a'transaction, r.d'transaction) is
    begin
        seen <= seen + 1;
    end process;

    fin: process is
    begin
        wait for 2 us;
        report "seen=" & integer'image(seen) & " a=" & to_hstring(r.a)
            & " b=" & std_logic'image(r.b) & " c=" & to_hstring(r.c)
            & " d=" & integer'image(r.d)
            & " a'last_active=" & time'image(r.a'last_active)
            & " c'last_active=" & time'image(r.c'last_active);
        wait;
    end process;
end architecture;
VHDL_EOF

export NVC_JIT_THRESHOLD=1
export NVC_JIT_ASYNC=0

nvc --std=2008 -a mt44.vhd -e mt44
NVC_THREADS=1 nvc --std=2008 -r mt44 > t1.out 2>&1
NVC_THREADS=2 nvc --std=2008 -r mt44 > t2.out 2>&1
NVC_THREADS=4 nvc --std=2008 -r mt44 > t4.out 2>&1

nvc --std=2008 --work=work_off -a mt44.vhd -e --rtl-islands=off mt44
nvc --std=2008 --work=work_off -r mt44 > off.out 2>&1

# Debug builds print extra lines that depend on the generated code
for f in t1 t2 t4 off; do
  grep -v "Debug:" $f.out > $f.txt
done

grep -q "seen=" t1.txt
diff t1.txt t2.txt
diff t1.txt t4.txt
diff t1.txt off.txt
