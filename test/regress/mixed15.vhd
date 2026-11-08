entity mixed_gen is
    generic (NAME : string := "default";
             GAIN : real := 1.0;
             W    : natural := 3;
             FLAG : boolean := false);
    port (o : out bit_vector(W - 1 downto 0));
end entity;

architecture test of mixed_gen is
begin
    process is
    begin
        if NAME = "default" then
            assert GAIN = 1.0 and W = 3 and not FLAG report "FAILED: default";
        elsif NAME = "hello ""x""" then
            assert GAIN = 2.5 and W = 5 and FLAG report "FAILED: named";
        elsif NAME = "abc" then
            assert GAIN = 3.0 and W = 3 and not FLAG report "FAILED: positional";
        else
            report "FAILED: NAME=" & NAME severity failure;
        end if;
        o <= (others => '1');
        wait;
    end process;
end architecture;
