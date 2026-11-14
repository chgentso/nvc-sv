library ieee;
use ieee.std_logic_1164.all;

entity mixedparam1 is
end entity;

architecture test of mixedparam1 is
    constant c_one : integer := 1;
    signal o1, o2, o3, o4, o5 : std_logic_vector(7 downto 0);
begin
    u1: entity work.mixedparam1v_m
        generic map (g_sim => '1', g_l => '1', g_n => 5)
        port map (o1);

    u2: entity work.mixedparam1v_m
        generic map (g_sim => bit'('0'), g_l => std_logic'('1'))
        port map (o2);

    u3: entity work.mixedparam1v_m
        generic map (g_sim => 1, g_l => 0, g_n => 2)
        port map (o3);

    u4: entity work.mixedparam1v_m
        port map (o4);

    u5: entity work.mixedparam1v_m
        generic map (g_sim => c_one, g_l => c_one - 1)
        port map (o5);

    process is
    begin
        wait for 1 ns;
        assert o1 = "01010110" report to_string(o1) severity failure;
        assert o2 = "00110100" report to_string(o2) severity failure;
        assert o3 = "00100010" report to_string(o3) severity failure;
        assert o4 = "00110000" report to_string(o4) severity failure;
        assert o5 = "00110010" report to_string(o5) severity failure;
        report "done";
        wait;
    end process;
end architecture;
