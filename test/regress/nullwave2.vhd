entity nullwave2 is
end entity;

architecture test of nullwave2 is
    type bv_array is array (natural range <>) of bit;
    type word_array is array (natural range <>) of bit_vector(31 downto 0);
    function resolve (x : word_array) return bit_vector is
    begin
        if x'length = 0 then
            return X"ffffffff";
        end if;
        return x(x'left);
    end function;
    subtype word is resolve bit_vector(31 downto 0);

    signal s : word bus;
begin

    p1: process is
    begin
        s <= X"12121212";
        wait for 1 ns;
        assert s = X"12121212";
        s <= null;
        wait for 1 ns;
        assert s = X"ffffffff";
        s <= X"34343434";                     -- Used to crash
        wait for 1 ns;
        assert s = X"34343434";
        s <= null after 1 ns;
        s <= null after 2 ns;
        wait for 3 ns;
        assert s = X"ffffffff";
        wait;
    end process;

end architecture;
