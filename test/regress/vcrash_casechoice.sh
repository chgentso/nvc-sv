set -x

# Relaxed rules allow a generic as a case choice: this must not crash
# the bounds checker, and the lowering reports it as not supported

cat >casechoice.vhd <<'VHD'
entity casechoice is
    generic ( foo : bit_vector(1 downto 0) := "10" );
end entity;

architecture a of casechoice is
    signal s : bit_vector(1 downto 0) := "01";
begin
    process (s) is
    begin
        case s is
            when foo    => report "foo";
            when "01"   => report "01";
            when others => report "other";
        end case;
    end process;
end architecture;
VHD

nvc --std=93 -a --relaxed casechoice.vhd 2> warn.txt
cat warn.txt
grep -q 'case choice must be locally static' warn.txt

# A generic of array type may be set from the command line
nvc --std=93 -e -gfoo=01 casechoice 2> err.txt || true
cat err.txt
if grep -q 'Fatal.*does not have item' err.txt; then exit 1; fi

if nvc --std=93 -e casechoice -r 2> err2.txt; then exit 1; fi
cat err2.txt
grep -q 'not currently supported' err2.txt
if grep -q 'Caught signal' err2.txt; then exit 1; fi
