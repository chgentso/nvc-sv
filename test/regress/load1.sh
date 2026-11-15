set -xe

# The test plugin selects its behaviour by test name
export TEST_NAME=vhpi2

nvc --std=08 -a $TESTDIR/regress/vhpi2.vhd -e vhpi2

# Both plugins are loaded whatever the order of the options
nvc -r vhpi2 --load=$NVC_PLUGIN_PATH/vhpi_test.so --load=siginit \
    +siginit+zero
nvc -r vhpi2 --load=siginit --load=$NVC_PLUGIN_PATH/vhpi_test.so \
    +siginit+zero
