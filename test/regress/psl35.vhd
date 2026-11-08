-- IEEE 1850-2010 6.1.1.2.5: parameterised SERE.
-- for i in S : op {r(i)} is {r(s1)} op {r(s2)} op ... for each value in
-- the value set S, where op is one of &&, & or |.

entity psl35 is
end entity;

architecture test of psl35 is
    signal clk    : bit := '0';
    signal v1     : bit_vector(0 to 2);
    signal v2, w2 : bit_vector(0 to 2);
    signal v3     : bit_vector(1 to 2);
    signal v4     : bit_vector(0 to 2);
    signal a5     : bit;
    signal v5     : bit_vector(0 to 1);

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 22 loop
            v1 <= "000"; v2 <= "000"; w2 <= "000"; v3 <= "00";
            v4 <= "000"; a5 <= '0'; v5 <= "00";
            case i is
                when 3 => v1 <= "111";
                when 5 => v1 <= "110";
                when 7 => v2 <= "010";
                when 8 => w2 <= "010";
                when 10 => v2 <= "100";
                when 11 => w2 <= "001";
                when 13 => v2 <= "001";
                when 14 => w2 <= "001";
                when 16 => v3 <= "11";
                when 17 => v3 <= "01";
                when 19 => v4 <= "101";
                when 20 => v4 <= "100"; a5 <= '1';
                when 21 => v5 <= "11";
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

    -- {v1(0)} && {v1(1)} && {v1(2)}: covered at 35 ns
    p1: cover {for i in {0 to 2} : && {v1(i)}} report "p1";
    -- {v2(0); w2(0)} | {v2(1); w2(1)} | {v2(2); w2(2)}: 85 and 145 ns
    p2: cover {for i in {0 to 2} : | {v2(i); w2(i)}} report "p2";
    -- {v3(1)[*1]} & {v3(2)[*2]}: covered at 175 ns
    p3: cover {for i in {1 to 2} : & {v3(i)[*i]}} report "p3";
    -- {v4(0)} && {v4(2)}: covered at 195 ns
    p4: cover {for i in {0, 2} : && {v4(i)}} report "p4";

    -- Must hold
    h1: assert always {a5} |=> {for i in {0 to 1} : && {v5(i)}};

end architecture;
