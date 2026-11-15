set -xe

# Attribute instances on modules, ports, nets and variables are kept and
# written by --netlist --evidence

which nvc

# Attributes on modules, ports (both styles), nets and variables
nvc --std=19 -a $TESTDIR/netlist/ev_svattr.sv -e ev_svattr_top
nvc --std=19 --netlist ev_svattr_top --output=plain3.json
[ $(grep -c '"port_attr"\|"keep"\|"a2"' plain3.json) = 0 ]
nvc --std=19 --netlist --evidence ev_svattr_top --output=ev3.json

grep -q '^"top_attr":"3"' ev3.json
grep -q '^"port_attr":"p"' ev3.json
grep -q '^"ansi_attr":"yes"' ev3.json
grep -q '^"async_reg":"TRUE"' ev3.json
grep -q '^"shreg_extract":"no"' ev3.json
[ $(grep -c '^"keep":"1"' ev3.json) = 2 ]
grep -q '^"a1":"1"' ev3.json
grep -q '^"a2":"x"' ev3.json
grep -q '^"mark_debug":"true"' ev3.json
# Names are in lower case
[ $(grep -c '^"ASYNC_REG"' ev3.json) = 0 ]
# A net declared without one has none
[ $(grep -c '^"keep"' ev3.json) = 2 ]

# The attributes survive the library: the module is analysed separately
nvc --std=19 -a $TESTDIR/netlist/ev_svattr2.sv
nvc --std=19 -a $TESTDIR/netlist/ev_svattr3.sv -e ev_svattr3
nvc --std=19 --netlist --evidence ev_svattr3 --output=ev4.json
[ $(grep -c '^"xpm_module":"TRUE"' ev4.json) = 2 ]
grep -q '^"keep_hierarchy":"SOFT"' ev4.json
