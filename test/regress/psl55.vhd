-- IEEE 1850-2010 6.2.1.5.1 to 6.2.1.5.3: strong forms until!, until!_,
-- before! and before!_, and abort of a sequence-based property.  All
-- must hold; see psl56 and psl57 for failing forms.

entity psl55 is
end entity;

architecture test of psl55 is
    signal clk                 : bit := '0';
    signal r1, a1, b1          : bit;
    signal r2, a2, b2          : bit;
    signal r3, a3, b3          : bit;
    signal r4, a4, b4          : bit;
    signal s5, t5, d5, rst5    : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 25 loop
            r1 <= '0'; a1 <= '0'; b1 <= '0';
            r2 <= '0'; a2 <= '0'; b2 <= '0';
            r3 <= '0'; a3 <= '0'; b3 <= '0';
            r4 <= '0'; a4 <= '0'; b4 <= '0';
            s5 <= '0'; t5 <= '0'; d5 <= '0'; rst5 <= '0';
            case i is
                when 2 => r1 <= '1';
                when 3 | 4 => a1 <= '1';
                when 5 => b1 <= '1';
                when 7 => r2 <= '1';
                when 8 | 9 => a2 <= '1';
                when 10 => a2 <= '1'; b2 <= '1';
                when 12 => r3 <= '1';
                when 14 => a3 <= '1';
                when 15 => b3 <= '1';
                when 17 => r4 <= '1';
                when 19 => a4 <= '1'; b4 <= '1';
                when 21 => s5 <= '1';
                when 22 => t5 <= '1';
                when 23 => d5 <= '1';
                when 24 => rst5 <= '1';
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

    u1: assert always (r1 -> next (a1 until! b1));
    u2: assert always (r2 -> next (a2 until!_ b2));
    v1: assert always (r3 -> next (a3 before! b3));
    v2: assert always (r4 -> next (a4 before!_ b4));

    -- The consequent would fail in cycle 24 but rst5 aborts it there
    ab: assert (always ({s5; t5} |=> {d5; d5})) abort rst5;

end architecture;
