-- Scope and visibility in PSL verification units.  A vunit bound to an
-- architecture sees the generics of the entity and the types, constants,
-- functions and signals declared in the architecture (extended scope),
-- and names made visible by its own context clause.  Names declared in a
-- vunit are local to it (immediate scope): two vunits bound to the same
-- design may declare the same identifiers and directive labels.
-- IEEE 1850-2010 9.1, 9.2, 9.3; IEEE 1076-2008 13.1, 13.4 (context clause
-- of a design unit).

library ieee;
use ieee.numeric_bit.all;

entity vunit8 is
    generic (WIDTH : positive := 4);
end entity;

architecture test of vunit8 is
    type state_t is (IDLE, RUN, DONE);
    constant LIMIT : natural := 5;

    function inc (x : natural) return natural is
    begin
        return x + 1;
    end function;

    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k
    signal st  : state_t;
    signal cnt : unsigned(WIDTH - 1 downto 0);
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    st <= IDLE when n = 0 else RUN when n < LIMIT else DONE;
    cnt <= to_unsigned(n mod 2 ** WIDTH, WIDTH);

end architecture;

-------------------------------------------------------------------------------

library ieee;
use ieee.numeric_bit.all;

vunit vu_a (vunit8(test)) {
    signal aux : natural;
    aux <= inc(n);

    default clock is rising_edge(clk);

    a1: assert always (aux = n + 1) report "vu_a a1 failed";
    a2: assert always (n >= LIMIT -> st = DONE) report "vu_a a2 failed";
    a3: assert always (cnt = to_unsigned(n, WIDTH)) report "vu_a a3 failed";
    c1: cover {st = IDLE; st = RUN} report "vu_a sees design scope";  -- 15 ns
}

vunit vu_b (vunit8(test)) {
    signal aux : state_t;               -- Same name as in vu_a
    aux <= st;

    default clock is rising_edge(clk);

    a1: assert always (aux = st) report "vu_b a1 failed";
    c1: cover {aux = RUN; aux = DONE} report "vu_b sees design scope";  -- 55 ns
}
