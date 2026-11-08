-- Analysis errors for PSL verification units: binding to a design unit
-- that does not exist, an assert directive in a vmode, an assume
-- directive in a vprop, inheriting a verification unit that does not
-- exist, and a reference to a name not visible in the vunit or the
-- design it is bound to.
-- IEEE 1850-2010 7.2, 7.2.1, 7.2.3, 9.

entity vunit9 is
end entity;

architecture test of vunit9 is
    signal clk : bit := '0';
    signal n   : natural := 0;
begin
end architecture;

vunit vu_nosuch (nosuch) {                      -- Error
}

vmode vm_bad (vunit9(test)) {
    default clock is rising_edge(clk);
    a1: assert always (n >= 0);                 -- Error
}

vprop vp_bad (vunit9(test)) {
    default clock is rising_edge(clk);
    as1: assume always (n >= 0);                -- Error
}

vunit vu_inherit (vunit9(test)) {
    inherit nosuch_vu;                          -- Error
}

vunit vu_undef (vunit9(test)) {
    default clock is rising_edge(clk);
    a1: assert always (zzz = 1);                -- Error
}
