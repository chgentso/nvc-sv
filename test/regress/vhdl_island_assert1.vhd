library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_assert1 is
end entity;

architecture test of vhdl_island_assert1 is
    signal clk  : std_logic := '0';
    signal cnt  : unsigned(7 downto 0) := (others => '0');
    signal next_cnt : unsigned(7 downto 0) := (others => '0');
    signal flag : std_logic := '0';
    signal done : boolean := false;
begin
    p_comb : process (all) is
    begin
        next_cnt <= cnt + 1;
        if cnt = x"02" then
            flag <= '1';
            report "comb hit cnt=" & to_hstring(cnt);
        else
            flag <= '0';
        end if;
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            cnt <= next_cnt;
            assert cnt /= x"ff" report "should not fire" severity failure;
            assert cnt /= x"02"
                report "ff assert cnt=" & to_hstring(cnt) & " flag=" & std_logic'image(flag)
                severity note;
            if cnt = x"03" then
                report "ff report val=" & integer'image(to_integer(cnt));
            end if;
        end if;
    end process;

    p_clk : process is
    begin
        while not done loop
            clk <= '0';
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
        end loop;
        wait;
    end process;

    p_check : process is
    begin
        for i in 1 to 4 loop
            wait until rising_edge(clk);
            wait for 1 ns;
        end loop;
        assert cnt = x"04" severity failure;
        done <= true;
        wait;
    end process;
end architecture;
