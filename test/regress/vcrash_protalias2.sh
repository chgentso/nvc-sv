set -x

# An alias in a protected type declaration for one of its own methods is
# diagnosed without an internal error

cat >protalias2.vhd <<'VHD'
entity protalias2 is end;

architecture a of protalias2 is
    type counter is protected
        procedure own;
        alias own_alias is own [];
    end protected;
begin
end architecture;
VHD

if nvc --std=19 -a protalias2.vhd 2> err.txt; then exit 1; fi
cat err.txt
grep -q 'must denote a protected type method' err.txt
if grep -q 'Fatal' err.txt; then exit 1; fi
