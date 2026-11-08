-- IEEE 1850-2010 5.1, 5.2.3: the operands of the built-in functions can
-- use the HDL logical operators and and or.

entity psl78 is
end entity;

architecture test of psl78 is
    signal clk     : bit := '0';
    signal a, b, c : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 9 loop
            a <= '0'; b <= '0'; c <= '0';
            case i is
                when 2 => a <= '1'; c <= '1';
                when 3 => a <= '1'; b <= '1';
                when 5 => b <= '1'; c <= '1';
                when 6 => b <= '1'; c <= '1';
                when 8 => a <= '1'; b <= '1';
                when others => null;
            end case;
            wait for 5 ns;
            clk <= '1';
            wait for 3 ns;
            clk <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    -- a or b rises in cycles 2, 5 and 8 but c does not hold in cycle 8
    a1: assert always rose(a or b) -> c report "a1";

    -- a and b held in cycles 3 and 8 but c does not hold in cycles 4
    -- and 9
    a2: assert always prev(a and b) -> c report "a2";

    -- These hold
    g1: assert always fell(a or b) -> not (a or b) report "g1";
    g2: assert always stable(a and b) or rose(a and b) or fell(a and b)
        report "g2";

end architecture;
