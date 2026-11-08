entity sem4 is
end entity;

architecture test of sem4 is
    signal x, y, clk : bit;
    signal i : integer;
    signal b : boolean;

    -- psl default clock is clk'event and clk = '1';
    -- psl sequence s1 (boolean p, q) is {p; q};
    -- psl sequence s2 (const numeric k) is {x[*k]};
    -- psl sequence s3 (sequence s) is {s; y};
    -- psl sequence s4 (hdltype integer v) is {i = v};
    -- psl property p1 (boolean p) is always p;
    -- psl property p2 (property q) is q;
    -- psl property p3 (numeric m) is always (i = m);
begin

    -- psl cover s1(x);                         -- Error (too few)
    -- psl cover s1(x, y, x);                   -- Error (too many)
    -- psl cover s2;                            -- Error (no actuals)
    -- psl cover s1(i, y);                      -- Error (not Boolean)
    -- psl cover s2(i);                         -- Error (not static)
    -- psl cover s2(x);                         -- Error (not Number)
    -- psl cover s3(p1(x));                     -- Error (not Sequence)
    -- psl cover s4(b);                         -- Error (type mismatch)
    -- psl assert p1(i);                        -- Error (not Boolean)
    -- psl assert p1;                           -- Error (no actuals)
    -- psl assert p3(x);                        -- Error (not Number)
    -- psl cover p2(always x);                  -- Error (property in cover)
    -- psl assert p2(always x);                 -- OK
    -- psl assert always s1(x, y);              -- OK (sequence as property)
    -- psl cover s3(s1(x, y));                  -- OK
    -- psl cover s3({x; y});                    -- OK
    -- psl assert always p3(5 + 2);             -- OK
    -- psl property p_rec is always p_rec;      -- Error (recursive)

end architecture;
