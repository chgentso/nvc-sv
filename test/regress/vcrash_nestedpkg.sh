set -xe

# Nested package declarations elaborate and run cleanly

cat >nestedpkg.vhd <<'VHD'
entity nestedpkg is end;

architecture a of nestedpkg is
    package pa is
        constant c : integer := 3;
    end package;
begin
    process is begin
        report integer'image(pa.c);
        wait;
    end process;
end architecture;
VHD

nvc --std=08 -a nestedpkg.vhd
nvc --std=08 -e nestedpkg
nvc --std=08 -r nestedpkg 2>&1 | grep -q 'Note:.*: 3'
