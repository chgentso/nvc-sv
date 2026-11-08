-- IEEE 1850-2010 6.1.1.2.4: SERE within.  {r1 within r2} matches when
-- r2 matches and r1 matches entirely inside it, ie. it is equivalent to
-- {[*]; r1; [*]} && {r2}.

entity psl34 is
end entity;

architecture test of psl34 is
    signal clk            : bit := '0';
    signal a1, b1, c1     : bit;
    signal a2, b2         : bit;
    signal a3, b3         : bit;
    signal a4, b4, c4     : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 22 loop
            a1 <= '0'; b1 <= '0'; c1 <= '0';
            a2 <= '0'; b2 <= '0';
            a3 <= '0'; b3 <= '0';
            a4 <= '0'; b4 <= '0'; c4 <= '0';
            case i is
                when 1 => a1 <= '1';
                when 2 => b1 <= '1';
                when 4 => c1 <= '1';
                when 6 => a1 <= '1';            -- No b1 inside 6..9
                when 9 => c1 <= '1';
                when 10 => b1 <= '1';
                when 11 | 12 => a2 <= '1';
                when 13 | 14 => a2 <= '1'; b2 <= '1';
                when 17 => a3 <= '1'; b3 <= '1';
                when 18 => b3 <= '1';
                when 19 => a3 <= '1';
                when 20 => a4 <= '1';
                when 21 => b4 <= '1';
                when 22 => c4 <= '1';
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

    w1: cover {{b1} within {a1; [*2]; c1}} report "w1";      -- 45 ns
    w2: cover {{b2; b2} within {a2[*4]}} report "w2";        -- 145 ns
    w3: cover {{b3} within {a3}} report "w3";                -- 175 ns

    -- Must hold
    h1: assert always {a4} |=> {{b4} within {[*2]}};

end architecture;
