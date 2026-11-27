-- Hand-written case: registered counter, a comb process and a monitor.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb is
end entity;

architecture sim of tb is
    signal clk   : std_logic := '0';
    signal rst   : std_logic := '1';
    signal cnt   : unsigned(7 downto 0);
    signal dbl   : unsigned(7 downto 0);
    signal wide  : unsigned(79 downto 0);
    signal ram_o : unsigned(7 downto 0);
    type ram_t is array (0 to 3) of unsigned(7 downto 0);
    signal ram   : ram_t;
begin
    clk <= not clk after 5 ns when now < 400 ns else '0';

    p_rst : process is
    begin
        wait for 22 ns;
        rst <= '0';
        wait;
    end process;

    p_cnt : process (clk) is
    begin
        if rising_edge(clk) then
            if rst = '1' then
                cnt <= (others => '0');
            else
                cnt <= cnt + 1;
            end if;
        end if;
    end process;

    p_dbl : process (all) is
    begin
        dbl <= cnt(6 downto 0) & '0';
    end process;

    p_wide : process (all) is
    begin
        wide <= resize(cnt, 80) * 16#10001# + 3;
    end process;

    p_ram : process (clk) is
    begin
        if rising_edge(clk) then
            ram(to_integer(cnt(1 downto 0))) <= dbl;
        end if;
    end process;

    ram_o <= ram(to_integer(cnt(3 downto 2)));

    p_mon : process (cnt) is
    begin
        if cnt'event and cnt(2 downto 0) = "111" then
            report "cnt=" & to_hstring(cnt) & " dbl=" & to_hstring(dbl)
                & " wide=" & to_hstring(wide);
        end if;
    end process;

    p_end : process is
    begin
        wait for 300 ns;
        report "end of test";
        std.env.finish;
    end process;
end architecture;
