set -x

# Unbounded recursion in the design is reported as a fatal error

# The address sanitizer has its own stack overflow handling
export ASAN_OPTIONS=handle_segv=0:detect_leaks=0

cat >recurse.vhd <<'VHD'
entity recurse is end;

architecture a of recurse is
    function f (x : integer) return integer is
    begin
        return f(x + 1);
    end function;
begin
    process is
    begin
        report integer'image(f(1));
        wait;
    end process;
end architecture;
VHD

nvc --std=08 -a recurse.vhd -e recurse

if nvc --std=08 -r recurse 2> err.txt; then exit 1; fi
cat err.txt
grep -q 'stack overflow' err.txt
