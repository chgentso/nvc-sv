-- The end-of-delta callback and the delta number of the nvc VPI extension.
-- The C side is test/vpi/vpi18.c.

entity vpi18 is
end entity;

architecture test of vpi18 is
    signal a, b, c : bit;
begin
    -- a chain of signals: c changes three delta cycles after a
    process is
    begin
        wait for 10 ns;
        a <= '1';
        wait for 10 ns;
        a <= '0';
        wait;
    end process;

    b <= a;
    c <= b;
end architecture;
