entity names1 is
end entity;

architecture test of names1 is
    signal clk, a : bit;
    default clock is clk'event;
begin
    assert names1;                      -- Error
    assert always names1;               -- Error
    assert always (names1);             -- Error
    assert always rose(names1);         -- Error
    assert always prev(names1) = '1';   -- Error
    assert always (names1 union a) = '1';  -- Error
    assert always (a union names1) = '1';  -- Error
    cover {a; names1};                  -- Error
    assert (always a) @ names1;         -- Error

    process is
    begin
        if names1 then                  -- Error
        end if;
        wait;
    end process;
end architecture;
