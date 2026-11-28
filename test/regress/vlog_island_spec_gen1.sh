set -xe

# Speculative variants of SV islands whose processes and signals come from
# generate blocks (NVC_ISLAND_SPEC=1) are lowered the first time the
# runtime calls them, like the others: the lowering reads the specialised
# copies of the generate items, which must outlive the lowering of the
# instance.  Output must be that of --rtl-islands=off in every mode.

cat >specgen1.sv <<'SV_EOF'
module specgen1_lane #(parameter W = 4, parameter MODE = 0)
  (input logic clk, rst, input logic [W-1:0] d, output logic [W-1:0] q);
  for (genvar i = 0; i < W; i++) begin : bit_g
    logic r, t;                    // Generate-local signals
    assign t = d[i] ^ (i % 2 == 0 ? r : ~r);
    always_ff @(posedge clk)
      if (rst) r <= 1'b0;
      else r <= t;
    assign q[i] = r;
  end
  if (MODE == 1) begin : acc_g
    logic [W-1:0] acc;
    always_ff @(posedge clk)
      if (rst) acc <= '0;
      else acc <= acc + d;
  end
endmodule

module specgen1;
  logic clk = 0, rst = 0;
  logic [3:0] d = 0;
  logic [5:0] e = 0;
  wire [3:0] q0, q1;
  wire [5:0] q2;

  specgen1_lane #(4, 0) l0 (.clk, .rst, .d, .q(q0));
  specgen1_lane #(4, 1) l1 (.clk, .rst, .d(~d), .q(q1));
  specgen1_lane #(6, 1) l2 (.clk, .rst, .d(e), .q(q2));

  initial begin
    for (int k = 0; k < 120; k++) begin
      rst = (k == 2 || k == 3);
      d = (k * 7 + 3) % 16;
      e = (k * 13 + 5) % 64;
      if (k == 40) d = 4'b1x0z;
      if (k == 70) e = 6'bxxxxxx;
      #1 clk = 1;
      #1 clk = 0;
      if (k % 10 == 0 || k == 41 || k == 71)
        $display("%0d q=%b %b %b acc=%h %h", k, q0, q1, q2, l1.acc_g.acc,
                 l2.acc_g.acc);
    end
    $finish;
  end
endmodule
SV_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_HOLD_WAIT NVC_RTL_ISLANDS
export NVC_ISLAND_SPEC_START=0   # Short run: promote from the start

nodebug() { grep -v '^\*\* Debug' || true; }   # Debug builds: JIT footprint

nvc --work=w_off -a specgen1.sv
nvc --work=w_off --rtl-islands=off -e specgen1 -r 2>&1 | nodebug > off.log

export NVC_ISLAND_SPEC=1
nvc --work=w_s -a specgen1.sv
nvc --work=w_s -e specgen1
for mode in "" NVC_ISLAND_SPEC_FORCE=A NVC_ISLAND_SPEC_STRESS=2; do
    env $mode NVC_ISLAND_SPEC_STATS=1 nvc --work=w_s -r specgen1 2>&1 \
        | nodebug > s_raw.log
    diff -u off.log <(grep -v '^ISLAND_SPEC' s_raw.log)
    cp s_raw.log "s_${mode:-default}.log"
done

grep ISLAND_SPEC s_default.log s_NVC_ISLAND_SPEC_FORCE=A.log
grep -q '^10 q=1010 0101 110110 acc=d 20' off.log
# Every island ran its variants
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=3 promoted=3 ' \
     s_NVC_ISLAND_SPEC_FORCE=A.log
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=3 promoted=3 ' s_default.log
