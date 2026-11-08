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
    signal aux : natural;
}

vunit vu_ok (vunit1(test)) {
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
    use vunit vu_ok;
    for test
    end for;
end configuration;

vunit vu_late (vunit1(test)) {
    signal s : bit;
    inherit vu_unbound;
}

vunit vu_self (vunit1(test)) {
    inherit vu_self;
}

vunit vu_cyc_a {
    inherit vu_cyc_b;
}

vunit vu_cyc_b {
    inherit vu_cyc_a;
}

vunit vu_cyc (vunit1(test)) {
    inherit vu_cyc_b;
}

vunit vu_insts (vunit1(test)) {
    i1 : vunit vu_unbound;
    i1 : vunit vu_unbound;
    i2 : vunit vu_insts;
    i3 : vunit nosuch_vu;
}

vunit vu_bad_body {
    default clock is clk'event and clk = '1';
    a1: assert always (yyy = 1);
}

vunit vu_use_bad (vunit1(test)) {
    inherit vu_bad_body;
}

vmode vm_dirs {
    default clock is clk'event and clk = '1';
    a1: assert always (n >= 0);
}

vunit vu_dirs (vunit1(test)) {
    inherit vm_dirs;
}
