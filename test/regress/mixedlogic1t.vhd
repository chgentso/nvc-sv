library ieee;
use ieee.std_logic_1164.all;

entity mixedlogic1 is
end entity;

architecture test of mixedlogic1 is
    signal clk, o, p, q, s, t, rw : std_logic := '0';
    signal w : std_logic_vector(3 downto 0) := "1111";
    signal v : std_logic_vector(3 downto 0);
begin
    u: entity work.mixedlogic1v_m
        port map (clk, o, v, p, q, s, t, w, rw);

    process is
    begin
        wait for 1 ns;
        assert o = '1';
        assert v = "1010";
        assert p = '1';
        assert q = '1';
        assert s = 'Z';
        assert t = '0';
        assert rw = '1' report "inout read in Verilog";
        report "done";
        wait;
    end process;
end architecture;
