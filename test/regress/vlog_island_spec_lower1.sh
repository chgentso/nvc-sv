set -xe

# NVC_ISLAND_SPEC=1 also lowers each SV island as two two-state variants on
# its four-state layout: "$a" loads only the value bits of a four-state
# signal and returns RT_ISLAND_DEOPT_XSTORE (12) instead of storing a value
# that may not be all 0/1 (division, x literal); a value computed only
# from loads and constants is stored without a check.  "$ah" checks the
# unknown bits of each load (RT_ISLAND_DEOPT_UNCLEAN, 9).  An island with a
# static variable in a process gets no variants.  The output is that of
# --rtl-islands=off.

cat >lower1.sv <<'EOF'
module dv (input logic clk, input logic [7:0] a, b,
           output logic [7:0] q, y);
  always_ff @(posedge clk) q <= a / b;   // May be x: checked store
  assign y = a + b;                      // 0/1 when a and b are
endmodule

module lit (input logic [1:0] sel, input logic [7:0] a,
            output logic [7:0] y);
  always_comb begin
    case (sel)
      2'b00:   y = a;
      2'b01:   y = ~a;
      default: y = 'x;
    endcase
  end
endmodule

module sloc (input logic [7:0] a, output logic [7:0] y);
  always_comb begin
    logic [7:0] t;                       // Static: no variants
    t = a ^ 8'h55;
    y = t;
  end
endmodule

module lower1;
  logic clk = 0;
  logic [1:0] sel = 0;
  logic [7:0] a = 0, b = 0;
  wire [7:0] q, y, ly, sy;
  dv u_dv (.clk, .a, .b, .q, .y);
  lit u_lit (.sel, .a, .y(ly));
  sloc u_sloc (.a, .y(sy));
  initial begin
    for (int k = 0; k < 8; k++) begin
      a = k * 37;
      b = k % 3;
      sel = k % 4;
      #1 clk = 1;
      #1 clk = 0;
      $display("%0d q=%h y=%h ly=%h sy=%h", k, q, y, ly, sy);
    end
    $finish;
  end
endmodule
EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS NVC_RTL_ISLANDS
nodebug() { grep -v '^\*\* Debug' || true; }

nvc --work=w_off -a lower1.sv
nvc --work=w_off --rtl-islands=off -e lower1 -r 2>&1 | nodebug > off.log

export NVC_ISLAND_SPEC=1
nvc --work=w_s -a lower1.sv
# NVC_DUMP_ISLANDS lowers the variants now instead of on their first call
NVC_DUMP_ISLANDS=1 NVC_LOWER_VERBOSE='$island' nvc --work=w_s -e lower1 \
    > lower.txt 2>&1
nvc --work=w_s -r lower1 2>&1 | nodebug > s.log
diff -u off.log s.log

unit() { sed -n "/^Name .*$1\$/,/^\$/p" lower.txt; }
# The flip-flop variant checks the quotient, the comb one does not check a+b
unit 'DV.$island_shared_ff_1$a' | grep -q 'return #12'
! unit 'DV.$island_shared_comb_0$a' | grep -q 'return #12'
unit 'DV.$island_shared_comb_0$ah' | grep -q 'return #9'
unit 'LIT.$island_shared_comb_2$a' | grep -q 'return #12'
! grep -q 'SLOC.*\$a$' lower.txt
grep -q 'SLOC.*island_comb0$' lower.txt
