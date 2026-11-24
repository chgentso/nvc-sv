#!/usr/bin/env bash
#
# Called by "nvc --install ise".
#

. "$(dirname "$BASH_SOURCE")/functions.sh"

if [ -z "$XILINX" ]; then
  cat >&2 <<EOF

Set the XILINX environment variable to the Xilinx ISE installation
directory or source the settings64.sh script.

EOF
  exit 1
elif [ ! -d "$XILINX/vhdl/src" ]; then
  cat >&2 <<EOF
$XILINX/vhdl/src not found

Ensure XILINX points at a valid Xilinx ISE installation directory.

EOF
  exit 1
fi

if [ -n "$MSYSTEM" ]; then
  XILINX="$(cygpath -u "$XILINX")"
fi

echo "Using ISE installation in $XILINX"
echo

src="$XILINX/vhdl/src"

GLOBAL_OPTS="-M 64m"
A_OPTS="--relaxed"

for STD in ${NVC_STD:-1993 2008}; do
    analyse_list unisim$(std_suffix $STD) <<EOF
$src/unisims/unisim_VPKG.vhd
$src/unisims/unisim_VCOMP.vhd
EOF

  cd "$src/unisims/primitive"
  analyse_list unisim$(std_suffix $STD) vhdl_analyze_order

  # Post-synthesis netlists written by ISE also name SIMPRIM
  if [ -f "$src/simprims/simprim_Vcomponents.vhd" ]; then
    analyse_list simprim$(std_suffix $STD) <<EOF
$src/simprims/simprim_Vpackage.vhd
$src/simprims/simprim_Vcomponents.vhd
EOF

    cd "$src/simprims/primitive/other"
    analyse_list simprim$(std_suffix $STD) vhdl_analyze_order
  fi
done
