entity vunit1 is
end entity;

architecture test of vunit1 is
    signal clk : bit := '0';
    signal n   : natural := 0;
begin
end architecture;

vunit vu_nosuch (nosuch) {
}

vmode vm_bad (vunit1(test)) {
    default clock is clk'event and clk = '1';
    a1: assert always (n >= 0);
    c1: cover {n = 1};
}

vprop vp_bad (vunit1(test)) {
    default clock is clk'event and clk = '1';
    as1: assume always (n >= 0);
}

vunit vu_inherit (vunit1(test)) {
    inherit nosuch_vu;
}

vunit vu_undef (vunit1(test)) {
    default clock is clk'event and clk = '1';
    a1: assert always (zzz = 1);
}

vunit vu_unbound {
    anything goes here { nested };
}

vunit vu_sorry (vunit1(test)) {
    inherit vu_unbound;
    i1 : vunit vu_unbound;
    override clk;
    override n;
}

vunit vu_arch (vunit1(nosuch_arch)) {
}

vunit vu_path (vunit1.nosuch) {
}

configuration vunit1_cfg of vunit1 is
    use vunit vu_unbound;
    use vunit nosuch_vu;
    for test
    end for;
end configuration;
