set -xe

cat >mypack.vhd <<EOT
package mypack is
    constant k : integer := 1;
end package;
EOT

cat >ent.vhd <<EOT
entity libdir6 is
end entity;

library mylib;
use mylib.mypack.all;

architecture test of libdir6 is
begin
    main: process is
    begin
        assert k = 1;
        wait;
    end process;
end architecture;
EOT

nvc --work=MYLIB -a mypack.vhd

# Empty entries in NVC_LIBPATH are ignored
P=$NVC_LIBPATH
NVC_LIBPATH= nvc -L$P -L. --work=WORK -a ent.vhd
NVC_LIBPATH=:: nvc -L$P -L. --work=WORK -a ent.vhd
NVC_LIBPATH=:$P:$PWD: nvc --work=WORK -a ent.vhd
NVC_LIBPATH=$P::$PWD nvc --work=WORK -e libdir6
