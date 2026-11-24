set -xe

which nvc

# LRM 93 section 1.3.2: rejected before VHDL-2008
! nvc --std=1993 -a $TESTDIR/regress/conf_default93.vhd 2>err
grep -q "cannot contain a block configuration before VHDL-2008" err

# LRM 08 section 3.4.3: accepted from VHDL-2008 onwards
nvc --std=2008 -a $TESTDIR/regress/conf_default93.vhd -e conf_default93_cfg -r conf_default93_cfg
