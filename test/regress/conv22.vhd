entity conv22_sub is
    port (
        b_in  : in  bit;
        b_out : out bit;
        i_in  : in  integer;
        i_out : out integer
    );
end entity;

architecture rtl of conv22_sub is
begin
    b_out <= b_in;
    i_out <= i_in;
end architecture;

entity conv22 is
end entity;

architecture test of conv22 is
    signal a, b : bit := '0';
    signal x, y : integer := 5;
begin
    u1: entity work.conv22_sub
        port map (
            b_in  => "not"(a),
            b_out => b,
            i_in  => "-"(x),
            "abs"(i_out) => y
        );

    process is
    begin
        wait for 1 ns;
        assert b = '1' severity failure;
        assert y = 5 severity failure;
        a <= '1';
        x <= -7;
        wait for 1 ns;
        assert b = '0' severity failure;
        assert y = 7 severity failure;
        wait;
    end process;
end architecture;
