entity block3 is
end entity;

architecture test of block3 is
    signal s    : natural := 0;
    signal last : time := 0 ns;
begin
    s <= 1 after 20 ns;

    b: block
        port (q : boolean);
        port map (q => s'quiet(10 ns));
    begin
        process (q) is
        begin
            last <= now;
        end process;
    end block;

    process is
    begin
        wait for 50 ns;
        assert last = 30 ns report time'image(last) severity failure;
        wait;
    end process;
end architecture;
