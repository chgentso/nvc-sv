-- Hand-written case: registered counter, comb processes, RAM, wide signal.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity m0 is
    port (clk, rst : in std_logic;
          o0 : out unsigned(7 downto 0);
          o1 : out unsigned(7 downto 0);
          o2 : out unsigned(79 downto 0);
          o3 : out unsigned(7 downto 0));
end entity;

architecture rtl of m0 is
    signal cnt  : unsigned(7 downto 0);
    signal dbl  : unsigned(7 downto 0);
    signal wide : unsigned(79 downto 0);
    type ram_t is array (0 to 3) of unsigned(7 downto 0);
    signal ram  : ram_t;
begin
    p0 : process (clk) is
    begin
        if rising_edge(clk) then
            if rst = '1' then
                cnt <= (others => '0');
            else
                cnt <= cnt + 1;
            end if;
        end if;
    end process;

    p1 : process (all) is
    begin
        dbl <= cnt(6 downto 0) & '0';
    end process;

    p2 : process (all) is
    begin
        wide <= shift_left(resize(cnt, 80), 70) + 3;
    end process;

    p3 : process (clk) is
    begin
        if rising_edge(clk) then
            ram(to_integer(cnt(1 downto 0))) <= dbl;
        end if;
    end process;

    o0 <= cnt;
    o1 <= dbl;
    o2 <= wide;
    o3 <= ram(to_integer(cnt(3 downto 2)));

    mon0 : process (cnt) is
    begin
        report "FZ M m0.cnt " & to_string(cnt);
    end process;
end architecture;
