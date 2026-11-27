#!/bin/sh
set -xe

# LRM 2019 6.5.3.2: constrained array subtype cannot map to unconstrained
# formal generic array type

cat > bad_gen_arr.vhd <<'EOF'
package gp is
    generic (
        type arr_t is array (natural range <>) of bit
    );
end package;

entity bad_gen_arr is
end entity;

architecture beh of bad_gen_arr is
    subtype bv4 is bit_vector(3 downto 0);
    package inst is new work.gp generic map ( arr_t => bv4 );
begin
end architecture;
EOF

! nvc --std=2019 -a bad_gen_arr.vhd 2>err.txt
grep -q "cannot map constrained array type" err.txt

cat > ok_gen_arr.vhd <<'EOF'
package gp_ok is
    generic (
        type arr_t is array (natural range <>) of bit
    );
end package;

entity ok_gen_arr is
end entity;

architecture beh of ok_gen_arr is
    package inst is new work.gp_ok generic map ( arr_t => bit_vector );
    signal s : inst.arr_t(1 downto 0) := "01";
begin
end architecture;
EOF

nvc --std=2019 -a ok_gen_arr.vhd -e ok_gen_arr -r ok_gen_arr
