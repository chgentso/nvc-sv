set -xe

pwd
which nvc

# Modules instantiated from Verilog are searched for in all libraries

cat >inv.vhd <<EOT
entity leaf is port (x : in bit; y : out bit); end;
architecture rtl of leaf is begin y <= not x; end;
EOT

cat >buf.vhd <<EOT
entity leaf is port (x : in bit; y : out bit); end;
architecture rtl of leaf is begin y <= x; end;
EOT

cat >top.v <<EOT
module top;
  reg a;
  wire y;
  leaf u(a, y);
  initial begin
    a = 0;
    #1;
    if (y !== 1) \$display("FAILED");
    else \$display("PASSED");
    \$finish;
  end
endmodule
EOT

cat >col.v <<EOT
module col; textio u(); endmodule
EOT

mkdir libs

# The first library in sorted order is used rather than the first in the
# order the directory is read
nvc --std=08 --work=libs/libz -a buf.vhd
nvc --std=08 --work=libs/libm -a buf.vhd
nvc --std=08 --work=libs/libb -a inv.vhd

# A library with the same unit analysed using another standard is skipped
nvc --std=93 --work=libs/liba -a buf.vhd

nvc --std=08 -L libs -a top.v
nvc --std=08 -L libs -e top
nvc --std=08 -L libs -r top 2>&1 | tee out
grep PASSED out
! grep FAILED out

# Units that cannot be instantiated are not found by the search
nvc --std=08 -a col.v
! nvc --std=08 -L libs -e col 2>err
grep "module 'textio' not found in library WORK or any other library" err
! grep "is not a Verilog module" err
