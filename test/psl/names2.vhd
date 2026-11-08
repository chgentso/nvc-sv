entity names2 is
end entity;

architecture test of names2 is
    signal clk, a : bit;
    default clock is rising_edge(clk);
    sequence s (boolean b) is {a; b};
    property p (const n) is always (a -> next[n] (a));
begin
    assert always nondet({names2}) = 0;     -- Error
    cover s(names2);                        -- Error
    assert p(names2);                       -- Error
    assert forall i in {names2} : always a; -- Error
end architecture;
