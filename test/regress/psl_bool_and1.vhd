library ieee;
use ieee.std_logic_1164.all;

entity psl_bool_and1 is
end entity;

architecture test of psl_bool_and1 is
    signal clk : std_logic := '0';
    signal a, c : std_logic := '0';
    signal b : std_logic_vector(3 downto 0) := x"0";
begin
    process
    begin
        for i in 0 to 7 loop
            if i = 1 then
                a <= '1';
                b <= x"4";
            else
                a <= '0';
            end if;
            if i = 2 or i = 4 then
                c <= '1';
                b <= x"4";
            else
                c <= '0';
            end if;
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
        end loop;
        wait;
    end process;

    default clock is rising_edge(clk);

    p1 : assert always ((a and b = x"4") -> next_event_a(c)[1 to 2](b = x"4"))
        report "p1 failed";
    p2 : assert always ((b = x"4" or c) -> (a or b = x"4"))
        report "p2 failed";
end architecture;
