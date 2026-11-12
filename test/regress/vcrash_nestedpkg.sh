set -x

# Nested package declarations are not supported by the code generator:
# elaboration must report this and not hit an internal error

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
if nvc --std=08 -e nestedpkg 2> err.txt; then exit 1; fi
cat err.txt
grep -q 'sorry, nested package declarations are not currently supported' err.txt
if grep -q 'does not have item' err.txt; then exit 1; fi
