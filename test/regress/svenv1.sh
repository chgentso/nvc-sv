set -xe

which nvc-sv

mkdir -p libdir1 libdir2

std=$(dirname $(which nvc-sv))/../lib

cat > pkg.vhd <<'EOF'
package p is
    constant c : integer := 5;
end package;
EOF

cat > use.vhd <<'EOF'
library mylib;
use mylib.p.all;
entity use1 is end entity;
architecture a of use1 is
begin
    process is begin
        assert c = 5;
        wait;
    end process;
end architecture;
EOF

nvc-sv --work=mylib:libdir1/mylib -a pkg.vhd

# Not found without a search path
! nvc-sv -a use.vhd

# Found through NVC_SV_LIBPATH
NVC_SV_LIBPATH=$PWD/libdir1:$std nvc-sv -a use.vhd -e use1 -r

# The upstream name is still accepted
NVC_LIBPATH=$PWD/libdir1:$std nvc-sv -a use.vhd -e use1 -r

# NVC_SV_LIBPATH wins when both are set
NVC_SV_LIBPATH=$PWD/libdir1:$std NVC_LIBPATH=$PWD/libdir2:$std nvc-sv -a use.vhd
! NVC_SV_LIBPATH=$PWD/libdir2:$std NVC_LIBPATH=$PWD/libdir1:$std nvc-sv -a use.vhd
