set -xe

# An others aggregate cannot be the default value of an unconstrained port
# as there is no subtype to take the index range from.  This is accepted
# with --relaxed if the port is associated with an actual

cat >sub.vhd <<EOT
entity sub is
    port ( a : in bit_vector := (others => '0');
           q : out integer );
end entity;

architecture t of sub is
begin
    q <= a'length;
end architecture;
EOT

cat >top1.vhd <<EOT
entity top1 is
end entity;

architecture t of top1 is
    signal r : integer;
begin
    u: entity work.sub port map ( a => "101", q => r );
end architecture;
EOT

cat >top2.vhd <<EOT
entity top2 is
end entity;

architecture t of top2 is
    signal r : integer;
begin
    u: entity work.sub port map ( a => open, q => r );
end architecture;
EOT

cat >top3.vhd <<EOT
entity top3 is
end entity;

architecture t of top3 is
    signal r : integer;
begin
    u: entity work.sub port map ( q => r );
end architecture;
EOT

# Standard mode
! nvc --std=08 -a sub.vhd 2>err.txt
grep "index range of array aggregate with others choice cannot be determined" err.txt
grep "allowed with the --relaxed option" err.txt

# Relaxed: a warning
nvc --std=08 -a --relaxed sub.vhd 2>warn.txt
grep "Warning: index range of array aggregate with others choice" warn.txt
nvc --std=08 -a --relaxed top1.vhd -e top1 -r

# but unconnected is still an error
! nvc --std=08 -a --relaxed top2.vhd 2>err2.txt
grep "cannot be unconnected as its default value has an others choice" err2.txt

! nvc --std=08 -a --relaxed top3.vhd 2>err3.txt
grep "missing actual for port A of unconstrained type" err3.txt

# A top-level port is unconnected
! nvc --std=08 -e sub 2>err4.txt
grep "unconnected top-level port A cannot have unconstrained type" err4.txt
