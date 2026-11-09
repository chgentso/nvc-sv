-- The diagnostic callback of the nvc VPI extension sees the messages of VHDL
-- assertions and report statements.  The C side is test/vpi/vpi15.c.

entity vpi15 is
end entity;

architecture test of vpi15 is
begin
    process is
    begin
        wait for 1 ns;
        report "just so you know" severity note;
        wait for 1 ns;
        assert false report "careful" severity warning;
        wait for 1 ns;
        assert false report "again" severity warning;
        wait for 1 ns;
        assert 1 = 1 report "not printed";
        wait;
    end process;
end architecture;
