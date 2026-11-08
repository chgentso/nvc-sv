-- LCS-2016-043: prefix rules for the PSL directive attributes 'SIGNAL and
-- 'EVENT (IEEE 1076-2019 16.2)
entity sem3 is
end entity;

architecture test of sem3 is
    signal clk, a, b : bit;
    default clock is clk'event and clk = '1';
begin

    c1: cover {a; b};                   -- OK
    a1: assert always a -> next b;      -- OK

    p1: process is
        variable v : boolean;
    begin
        v := c1'signal;                 -- OK
        v := a1'signal;                 -- OK
        v := c1'event;                  -- OK
        v := a'signal;                  -- Error
        v := p1'signal;                 -- Error
        c1'signal <= true;              -- Error
        wait on c1'signal;              -- OK
        wait;
    end process;

end architecture;
