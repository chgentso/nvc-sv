-- Hand-written case: a combinational chain of four processes that read each
-- other.  The monitor process mon0 is an unfused observer of d; it reports
-- each event together with the delta counter i0 driven by the testbench.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity m0 is
    port (clk : in std_logic;
          i0  : in unsigned(3 downto 0);
          o0  : out unsigned(7 downto 0));
end entity;

architecture rtl of m0 is
    signal q : unsigned(7 downto 0) := (others => '0');
    signal a, b, c, d : unsigned(7 downto 0) := (others => '0');
begin
    p0 : process (clk) is
    begin
        if rising_edge(clk) then
            q <= q + 3;
        end if;
    end process;

    p1 : process (all) is
    begin
        a <= q + 1;
    end process;

    p2 : process (all) is
    begin
        b <= a xor q;
    end process;

    p3 : process (all) is
    begin
        c <= b + a;
    end process;

    p4 : process (all) is
    begin
        d <= c - b;
    end process;

    o0 <= d;

    mon0 : process (d) is
    begin
        report "FZ M m0.d " & to_string(d) & " i0=" & to_string(i0);
    end process;
end architecture;
