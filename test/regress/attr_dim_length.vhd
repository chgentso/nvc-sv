entity attr_dim_length is
end entity;

architecture test of attr_dim_length is
    type m2 is array (1 to 2, 5 downto 3) of bit;
begin
    process is
        variable mm : m2;
        variable bv : bit_vector(0 to 1);
        type lit is (a, b);
    begin
        -- 'LENGTH of a locally static subtype is a locally static
        -- universal_integer expression
        assert mm'high(bv'length) = 5;
        assert mm'low(bv'length) = 3;
        assert mm'length(1 + 1) = 3;
        assert mm'right(lit'pos(b) + 1) = 3;
        wait;
    end process;
end architecture;
