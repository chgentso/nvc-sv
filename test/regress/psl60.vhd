-- IEEE 1850-2010 6.2.1.8: LTL operators.  X is next, X! is next!, F is
-- eventually!, G is always, [f U g] is f until! g and [f W g] is f
-- until g.  All must hold; see psl61 for the failing forms.

entity psl60 is
end entity;

architecture test of psl60 is
    signal clk                 : bit := '0';
    signal a1, b1              : bit;
    signal a2, b2              : bit;
    signal a3, b3              : bit;
    signal a4, b4, c4          : bit;
    signal a5, b5, c5          : bit;
    signal e7                  : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 23 loop
            a1 <= '0'; b1 <= '0';
            a2 <= '0'; b2 <= '0';
            a3 <= '0'; b3 <= '0';
            a4 <= '0'; b4 <= '0'; c4 <= '0';
            a5 <= '0'; b5 <= '0'; c5 <= '0';
            e7 <= '0';
            case i is
                when 2 => a1 <= '1';
                when 3 => b1 <= '1';
                when 5 => a2 <= '1';
                when 6 => b2 <= '1';
                when 8 => a3 <= '1';
                when 11 => b3 <= '1';
                when 13 => a4 <= '1';
                when 14 | 15 => c4 <= '1';
                when 16 => b4 <= '1';
                when 20 => a5 <= '1'; e7 <= '1';
                when others => null;
            end case;
            if i > 20 then
                c5 <= '1';
            end if;
            wait for 5 ns;
            clk <= '1';
            wait for 3 ns;
            clk <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    t1: assert G (a1 -> X b1);
    t2: assert G (a2 -> X! b2);
    t3: assert G (a3 -> F b3);
    t4: assert G (a4 -> X [c4 U b4]);
    t5: assert G (a5 -> X [c5 W b5]);    -- Weak: b5 never set
    t6: assert F e7;

end architecture;
