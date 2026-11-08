-- IEEE 1850-2010 6.2.1.7: logical or of two temporal properties and
-- logical not of a Sequence.  These are outside the simple subset
-- (section 4.4.4) but can be checked in simulation: the or fails when
-- both operands have failed, and not {r} fails when a match of r ends.

entity psl85 is
end entity;

architecture test of psl85 is
    signal clk              : bit := '0';
    signal s1, a, b         : bit;
    signal s2, c, d, e, f   : bit;
    signal s3, g, h         : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 11 loop
            s1 <= '0'; a <= '0'; b <= '0';
            s2 <= '0'; c <= '0'; d <= '0'; e <= '0'; f <= '0';
            s3 <= '0'; g <= '0'; h <= '0';
            case i is
                when 2 => s1 <= '1';
                when 3 => b <= '1'; s3 <= '1'; g <= '1';
                when 4 => h <= '1';
                when 5 => s1 <= '1';
                when 7 => s2 <= '1'; c <= '1';
                when 8 => d <= '1'; s3 <= '1'; g <= '1';
                when 9 => s2 <= '1'; c <= '1';
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

    -- Neither a nor b holds in cycle 6
    a1: assert always (s1 -> ((next a) or (next b))) report "a1";

    -- {c; d} fails in cycle 10 and {e; f} in cycle 9
    a2: assert always (s2 -> ({c; d} or {e; f})) report "a2";

    -- {g; h} matches from cycle 3 to 4
    a3: assert always (s3 -> not {g; h}) report "a3";

    -- These hold
    g1: assert always (s2 -> ({c; d} or {c; true})) report "g1";
    g2: assert always (s1 -> ((next b) or (next not b))) report "g2";

end architecture;
