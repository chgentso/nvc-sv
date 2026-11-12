set -x

# Syntax error that leaves a procedure call statement without a name that
# can be resolved must not crash the parser

cat >proccall.vhd <<'VHD'
entity proccall is end;

architecture a of proccall is
begin
    process is
    begin
        assert false
            report "unterminated
    end process;
end architecture;
VHD

if nvc --std=93 -a proccall.vhd 2> err.txt; then exit 1; fi
cat err.txt
grep -q 'unterminated string literal' err.txt
if grep -q 'Caught signal' err.txt; then exit 1; fi
