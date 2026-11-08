-- The PSL keywords that IEEE 1076-2008 adds as reserved words (assume,
-- fairness, property, restrict, sequence, strong, vmode, vprop, vunit)
-- and the PSL-only keywords inherit and override remain ordinary
-- identifiers in earlier VHDL revisions.
-- IEEE 1076-2008 15.10; IEEE 1076-1993 13.9.

entity psl50 is
end entity;

architecture test of psl50 is
    signal assume, fairness, restrict, strong : boolean := false;
    signal property, sequence : integer := 0;
    signal vmode, vprop, vunit : bit := '0';
    signal inherit, override : character := 'x';
begin

    process is
    begin
        assume <= true;
        fairness <= true;
        restrict <= true;
        strong <= true;
        property <= 1;
        sequence <= 2;
        vmode <= '1';
        vprop <= '1';
        vunit <= '1';
        inherit <= 'i';
        override <= 'o';
        wait for 1 ns;
        assert assume and fairness and restrict and strong;
        assert property + sequence = 3;
        assert (vmode and vprop and vunit) = '1';
        assert inherit = 'i' and override = 'o';
        report "PASSED";
        wait;
    end process;

end architecture;
