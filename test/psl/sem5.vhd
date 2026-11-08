entity sem5 is
end entity;

architecture test of sem5 is
    signal x, y, clk : bit;
    signal i         : integer;
    signal v         : bit_vector(0 to 3);

    -- psl default clock is clk'event and clk = '1';    -- OK
    -- psl default clock is clk'event and clk = '0';    -- Error
begin

    -- psl assert always prev(x, 0) = '0';              -- Error
    -- psl assert always (x -> next(y) = '1');          -- Error
    -- psl assert always stable(i);                     -- OK
    -- psl assert always nondet_vector(i, {0}) = "0";   -- Error
    -- psl assert always nondet({0 to i}) = 0;          -- Error
    -- psl assert always {x; (y -> x)};                 -- OK
    -- psl assert always not (x -> next y);             -- Error
    -- psl assert always (x <-> next y);                -- Error
    -- psl assert always ((x -> next y) and (y -> next x));  -- OK
    -- psl assert G (x -> F (y -> next x));             -- Error
    -- psl assert G [x U (next y)];                     -- Error
    -- psl assert G [(next x) W y];                     -- OK
    -- psl assert always (x -> next_event_e(y)[1 to 2] (next x));  -- Error
    -- psl assert always (x -> next_event_a(y)[1 to 2] (next x));  -- OK
    -- psl assert forall k in {0 to i} : always v(k);   -- Error
    -- psl cover {for k in {0 to i} : && {v(k)}};       -- Error
    -- psl assert always (for k in {0 to i} : and (v(k)));  -- Error
    -- psl assert AG x;                                 -- Error
    -- psl assert always (forall k in {0 to 3} : v(k));  -- Error

end architecture;
