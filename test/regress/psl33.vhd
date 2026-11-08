-- IEEE 1850-2010 6.1.1.2.2 and 6.1.1.2.3: SERE non-length-matching and
-- (&) and length-matching and (&&).  For &, both operands start in the
-- same cycle and the match ends when the longer one ends; for && both
-- operands must match with the same length.

entity psl33 is
end entity;

architecture test of psl33 is
    signal clk                : bit := '0';
    signal a1, b1, c1, d1     : bit;
    signal a2, b2             : bit;
    signal a3, b3, c3         : bit;
    signal a4, b4, c4         : bit;
    signal a5, b5, c5         : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 22 loop
            a1 <= '0'; b1 <= '0'; c1 <= '0'; d1 <= '0';
            a2 <= '0'; b2 <= '0';
            a3 <= '0'; b3 <= '0'; c3 <= '0';
            a4 <= '0'; b4 <= '0'; c4 <= '0';
            a5 <= '0'; b5 <= '0'; c5 <= '0';
            case i is
                when 1 => a1 <= '1'; d1 <= '1';
                when 2 => b1 <= '1';
                when 3 => c1 <= '1';
                when 5 => a1 <= '1';
                when 6 => b1 <= '1';
                when 7 => c1 <= '1';
                when 9 => a2 <= '1'; b2 <= '1';
                when 10 | 11 => a2 <= '1';
                when 13 => a3 <= '1'; c3 <= '1';
                when 14 => b3 <= '1'; c3 <= '1';
                when 16 => a4 <= '1'; c4 <= '1';
                when 17 => b4 <= '1';
                when 19 => a5 <= '1';
                when 20 => b5 <= '1'; c5 <= '1';
                when 21 => c5 <= '1';
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

    n1: cover {{a1; b1; c1} & {d1}} report "n1";           -- 35 ns
    n2: cover {{a2[*1 to 3]} & {b2}} report "n2";          -- 95, 105, 115 ns
    n3: cover {{a3; b3} && {c3; c3}} report "n3";          -- 145 ns
    n4: cover {{a4; b4} & {c4}} report "n4";               -- 175 ns

    -- Must hold
    h1: assert always {a5} |=> {{b5} & {c5; c5}};

end architecture;
