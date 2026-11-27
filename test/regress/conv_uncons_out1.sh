#!/bin/sh
set -xe

# LRM 93 section 3.2.1.1: output conversion for an unconstrained formal
# port must denote a constrained array subtype

cat > bad_out_conv.vhd <<'EOF'
entity child is
    port ( o : out bit_vector );
end entity;

architecture beh of child is
begin
    o <= (others => '0');
end architecture;

entity parent is
    port ( s : out bit_vector(3 downto 0) );
end entity;

architecture beh of parent is
    type bv2 is array (natural range <>) of bit;
    signal x : bv2(3 downto 0);
begin
    u1: entity work.child
        port map ( bv2(o) => x );
end architecture;
EOF

! nvc --std=1993 -a bad_out_conv.vhd 2>err.txt
grep -q "result of conversion for unconstrained formal O must be a constrained array type" err.txt
