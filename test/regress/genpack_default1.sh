#!/bin/sh
set -e

# Default component binding must report a clean error rather than crashing
# when an entity has an interface package generic with no matching generic
# in the component declaration.

cat > test.vhd <<'EOF'
package gp_def1_pkg is
    generic (N : positive);
    subtype vec_t is bit_vector(N - 1 downto 0);
end package;

entity gp_def1_sub is
    generic (
        W : positive;
        package vp is new work.gp_def1_pkg generic map (N => W)
    );
    port (
        o : out vp.vec_t
    );
end entity;

architecture rtl of gp_def1_sub is
begin
    o <= (others => '1');
end architecture;

entity gp_def1_top is
end entity;

architecture tb of gp_def1_top is
    package vp4 is new work.gp_def1_pkg generic map (N => 4);

    component gp_def1_sub is
        generic (W : positive);
        port (o : out vp4.vec_t);
    end component;

    signal s : bit_vector(3 downto 0);
begin
    u1 : gp_def1_sub
        generic map (W => 4)
        port map (o => s);
end architecture;
EOF

nvc --std=2008 -a test.vhd
if nvc --std=2008 -e gp_def1_top >elab.err 2>&1; then
    echo "FAIL: elaboration unexpectedly succeeded"
    exit 1
fi
grep -q "has no corresponding generic in component" elab.err || {
    echo "FAIL: expected clean error, got:"
    cat elab.err
    exit 1
}
