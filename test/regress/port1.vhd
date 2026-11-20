entity port1_sub is
    port (
        i : in  bit_vector;
        o1 : out bit_vector;
        o2 : out bit_vector
    );
end entity;

architecture test of port1_sub is
begin
    o1 <= i(o1'range);
    o2 <= (o2'range => '1');
    check : process is
    begin
        assert o1'length = 1;
        assert o1'left = 0 and o1'right = 0;
        assert o2'length = 2;
        assert o2'left = 0 and o2'right = 1;
        wait;
    end process;
end architecture;

entity port1 is
end entity;

architecture test of port1 is
    signal x : bit_vector(0 to 3) := "1010";
    signal y : bit := '0';
begin
    u : entity work.port1_sub
        port map (
            i     => x,
            o1(0) => open,
            o2(1) => y,
            o2(0) => open
        );

    check : process is
    begin
        wait for 1 ns;
        assert y = '1';
        wait;
    end process;
end architecture;
