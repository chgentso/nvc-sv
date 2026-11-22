set -xe

cat > defs.svh <<'EOF'
`define CHECK_VAL(x) if ((x) !== 8'h5A) $fatal(1, "FAIL: x=%h", (x));
EOF

cat > pkg.sv <<'EOF'
package svsu1_pkg;
  typedef struct packed {
    logic [7:0] data;
  } strap_t;
endpackage
EOF

cat > ifc.sv <<'EOF'
import svsu1_pkg::*;

interface svsu1_if;
  strap_t s;
endinterface
EOF

cat > tb.sv <<'EOF'
module svsingleunit1;
  strap_t local_strap;
  svsu1_if u_if();

  initial begin
    local_strap.data = 8'h5A;
    u_if.s = local_strap;
    #1;
    `CHECK_VAL(u_if.s.data)
    $display("PASSED");
  end
endmodule
EOF

nvc -a --single-unit defs.svh pkg.sv ifc.sv tb.sv -e svsingleunit1 -r svsingleunit1 2>&1 | tee out
grep -q "PASSED" out
if grep -qi "failure\|fatal" out; then exit 1; fi
