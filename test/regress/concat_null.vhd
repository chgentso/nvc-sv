entity concat_null is
end entity;

architecture test of concat_null is
    type r1 is range 0 to 7;
    type r2 is range 7 downto 0;
    type t1 is array (r1 range <>) of bit;
    type t2 is array (r2 range <>) of bit;

    function l1 (x : t1) return integer is
    begin
        return integer(x'left);
    end function;

    function r1f (x : t1) return integer is
    begin
        return integer(x'right);
    end function;

    function len1 (x : t1) return integer is
    begin
        return x'length;
    end function;

    function l2 (x : t2) return integer is
    begin
        return integer(x'left);
    end function;
begin
    process is
        variable v1 : t1(0 to 7) := (others => '0');
        variable v2 : t2(7 downto 0) := (others => '0');
    begin
        -- A null operand is not an error for the result bounds check
        assert len1(v1(2 to 1) & v1(1 to 2)) = 2;
        assert l1(v1(2 to 1) & v1(1 to 2)) = 0;
        assert r1f(v1(2 to 1) & v1(1 to 2)) = 1;
        assert l2(v2(2 downto 3) & v2(3 downto 2)) = 7;
        -- Both operands null: the result is the right operand
        assert len1(v1(2 to 1) & v1(5 to 4)) = 0;
        assert l1(v1(2 to 1) & v1(5 to 4)) = 5;
        assert r1f(v1(2 to 1) & v1(5 to 4)) = 4;
        assert l1(v1(1 to 0) & v1(8 to 7)) = 8;
        assert l2(v2(2 downto 3) & v2(5 downto 6)) = 5;
        wait;
    end process;
end architecture;
