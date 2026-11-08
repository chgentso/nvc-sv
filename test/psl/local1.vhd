entity local1 is
end entity;

architecture test of local1 is
    signal clk, a, b : bit;
    signal x : integer;

    -- psl default clock is clk'event and clk = '1';
begin

    -- psl c1: cover {[[variable v : integer;]] a [[v := x;]]; b};
    -- psl a1: assert always ([[variable v : integer;]] a -> next (x = 1));

end architecture;
