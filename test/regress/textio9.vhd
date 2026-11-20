use std.textio.all;

entity textio9 is
end entity;

architecture test of textio9 is
begin
    process is
        variable l : line;
        variable s : string(1 to 4);
        variable w : string(1 to 4);
        variable n : natural;
    begin
        l := new string(1 to 13);
        l.all := "hereisastring";
        read(l, s);
        assert s = "here" severity failure;
        assert l.all'left = 5 and l.all'right = 13 severity failure;
        assert l.all(5 to 13) = "isastring" severity failure;
        deallocate(l);

        l := new string(10 downto 1);
        l.all := "0123456789";
        read(l, s);
        assert s = "0123" severity failure;
        assert l.all'left = 6 and l.all'right = 1 severity failure;
        assert l.all(6 downto 1) = "456789" severity failure;
        deallocate(l);

        l := new string'("  word rest");
        sread(l, w, n);
        assert n = 4 and w = "word" severity failure;
        assert l.all'left = 7 severity failure;
        deallocate(l);
        wait;
    end process;
end architecture;
