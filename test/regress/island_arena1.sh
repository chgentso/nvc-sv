set -xe

# Island objects made at elaboration come from a huge-page arena
# (NVC_ISLAND_ARENA, default on outside ASan).  Output and waves must be
# the same with the arena off and with --rtl-islands=off, also for a
# sparse VHDL RAM (its state is replaced after the island is made), with
# speculative variants (NVC_ISLAND_SPEC=1 grows the state buffer) and with
# a waveform dump (descriptors rebuilt after an observer attaches).

cat >arena1.sv <<'EOF2'
module arena1_inc (input logic [7:0] a, output logic [7:0] y);
  assign y = a + 8'd3;
endmodule

module arena1_reg (input logic clk, input logic rst, input logic [7:0] d,
                   output logic [7:0] q);
  always_ff @(posedge clk) q <= rst ? 8'd0 : d;
endmodule

module arena1;
  logic clk = 0, rst = 1;
  logic [7:0] q, y1, y2;

  arena1_reg u_reg (.clk, .rst, .d(y2), .q);
  arena1_inc u_inc1 (.a(q), .y(y1));
  arena1_inc u_inc2 (.a(y1), .y(y2));

  always #5 clk = ~clk;
  always @(q) if (q[2:0] == 3'd6) $display("%0t q=%0d", $time, q);

  initial begin
    #12 rst = 0;
    #300 $display("q=%0d y2=%0d", q, y2);
    $finish;
  end
endmodule
EOF2

strip_date() {
  sed -e '/^\$date/,/\$end/d' "$1" > "$2"
}

export NVC_RTL_ISLANDS=hier

run_all() {
  local tag=$1 top=$2; shift 2
  for a in 0 1; do
    NVC_ISLAND_ARENA=$a nvc "$@" -r $top --wave=$tag-$a.vcd --format=vcd \
      >$tag-$a.txt 2>&1
    strip_date $tag-$a.vcd $tag-$a.norm
  done
  NVC_RTL_ISLANDS=off nvc "$@" -r $top --wave=$tag-off.vcd --format=vcd \
    >$tag-off.txt 2>&1
  strip_date $tag-off.vcd $tag-off.norm
  diff -u $tag-0.txt $tag-1.txt
  diff -u $tag-off.txt $tag-1.txt
  diff -u $tag-0.norm $tag-1.norm
  diff -u $tag-off.norm $tag-1.norm
}

nvc --work=sv -a arena1.sv -e arena1
run_all sv arena1 --work=sv
grep -q '^q=180 y2=186$' sv-1.txt

nvc --std=2008 --work=ram -a $TESTDIR/regress/vhdl_island_sparse_ram1.vhd \
  -e vhdl_island_sparse_ram1
run_all ram vhdl_island_sparse_ram1 --std=2008 --work=ram

export NVC_ISLAND_SPEC=1
nvc --std=2008 --work=spec -a $TESTDIR/regress/vhdl_island_sparse_ram1.vhd \
  -e vhdl_island_sparse_ram1
run_all spec vhdl_island_sparse_ram1 --std=2008 --work=spec
