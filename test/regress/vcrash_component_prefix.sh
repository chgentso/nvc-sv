set -x

# A component name used as the prefix of a procedure call

cat >compprefix.vhd <<'VHD'
entity compprefix is end;

architecture a of compprefix is
    component comp is end component;
begin
    comp.ex;
end architecture;
VHD

if nvc --std=93 -a compprefix.vhd 2> err.txt; then exit 1; fi
cat err.txt
grep -q 'cannot be the prefix of a selected name' err.txt
if grep -q 'Caught signal' err.txt; then exit 1; fi
