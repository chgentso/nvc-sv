-- LCS-2016-016: anonymous types on generic constants and on subprogram
-- parameters associated by name.  The actual for the implicit generic
-- type is the subtype of the actual associated with the first formal
-- of that type, which also determines the type of later actuals such as
-- literals (IEEE 1076-2019 6.5.2, 6.5.7.1).

entity anon_gen is
    generic (
        g : type is private;
        h : type is range <> );
    port (
        same : out boolean;
        sum  : out integer );
end entity;

architecture test of anon_gen is
begin
    same <= g = g;
    sum  <= integer(h + h);
end architecture;

-------------------------------------------------------------------------------

entity anontype3 is
end entity;

architecture test of anontype3 is
    constant c : bit_vector := "101";
    subtype small_t is integer range 0 to 9;

    signal same : boolean;
    signal sum  : integer;

    procedure swap (x, y : inout type is private) is
        variable tmp : x'subtype;
    begin
        tmp := x;
        x := y;
        y := tmp;
    end procedure;

    function larger (a, b : type is <>) return boolean is
    begin
        return a > b;
    end function;
begin

    u1: entity work.anon_gen
        generic map (g => c, h => small_t'(4))
        port map (same, sum);

    process is
        variable i1, i2 : integer;
        variable b      : bit;
        variable ch     : character;
    begin
        i1 := 1;
        i2 := 2;
        swap(y => i1, x => i2);
        assert i1 = 2 and i2 = 1;

        -- The literal takes the type of the other actual
        b := '0';
        assert larger('1', b);
        assert not larger(b, '1');
        ch := 'm';
        assert larger(ch, 'a');
        assert larger(i1, 1);

        wait for 1 ns;
        assert same;
        assert sum = 8;

        wait;
    end process;

end architecture;
