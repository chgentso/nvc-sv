library ieee;
use ieee.std_logic_1164.all;

entity mixedlogic1 is
end entity;

architecture test of mixedlogic1 is
    signal clk, o, p, q, s, t : std_logic := '0';
    signal v : std_logic_vector(3 downto 0);
begin
    u: entity work.mixedlogic1v_m
        port map (clk, o, v, p, q, s, t);

    process is
    begin
        wait for 1 ns;
        assert o = '1';
        assert v = "1010";
        assert p = '1';
        assert q = '1';
        assert s = 'Z';
        assert t = '0';
        report "done";
        wait;
    end process;
end architecture;
