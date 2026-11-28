set -xe

# Promotion checks of speculative SV island variants (NVC_ISLAND_SPEC=1)
# are scheduled per island: a sweep looks at the islands whose check is
# due, not at every island.  64 counters promote after reset; one island
# reads a register that is never written, waits before it is promoted
# holding it (checks at growing intervals) and deopts when the x reaches
# its output.  The waits must not visit the other 64 islands each time.
# A run this short promotes nothing unless NVC_ISLAND_SPEC_START says so.
# Output must be that of --rtl-islands=off in every mode.

cat >sweep1.sv <<'SV_EOF'
module sweep1_cnt #(parameter INC = 1) (input logic clk, rst,
                                        output logic [7:0] q);
  always_ff @(posedge clk)
    if (rst) q <= 8'h00;
    else q <= q + INC;
endmodule

module sweep1_xr (input logic clk, input logic [7:0] d,
                  output logic [7:0] y);
  logic [7:0] ru;                  // Never written: x for the whole run
  logic [7:0] r = 8'h00;
  always_ff @(posedge clk) r <= r + d;
  assign y = d[7] ? (ru ^ r) : r;
endmodule

module sweep1;
  logic clk = 0, rst = 0;
  logic [7:0] d = 0;
  wire [7:0] q [0:63];
  wire [7:0] y;
  logic [7:0] sum;

  for (genvar i = 0; i < 64; i++) begin : g
    sweep1_cnt #(i + 1) u (.clk, .rst, .q(q[i]));
  end
  sweep1_xr x (.clk, .d, .y);

  always_comb begin
    sum = 0;
    for (int i = 0; i < 64; i++) sum = sum ^ q[i];
  end

  initial begin
    #1 rst = 1;
    #1 clk = 1;
    #1 clk = 0;
    #1 rst = 0;
    for (int k = 0; k < 300; k++) begin
      d = (k * 37 + 11) % 128;
      if (k == 250) d = 8'h85;
      #1 clk = 1;
      #1 clk = 0;
      if (k % 50 == 0 || k == 250) $display("%0d sum=%h y=%b", k, sum, y);
    end
    $finish;
  end
endmodule
SV_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_HOLD_WAIT NVC_RTL_ISLANDS
export NVC_ISLAND_SPEC_START=0   # Short run: promote from the start

nodebug() { grep -v '^\*\* Debug' || true; }   # Debug builds: JIT footprint

nvc --work=w_off -a sweep1.sv
nvc --work=w_off --rtl-islands=off -e sweep1 -r 2>&1 | nodebug > off.log

export NVC_ISLAND_SPEC=1
nvc --work=w_s -a sweep1.sv
nvc --work=w_s -e sweep1
for mode in "" NVC_ISLAND_SPEC_FORCE=A NVC_ISLAND_SPEC_STRESS=2; do
    env $mode NVC_ISLAND_SPEC_STATS=1 nvc --work=w_s -r sweep1 2>&1 \
        | nodebug > s_raw.log
    diff -u off.log <(grep -v '^ISLAND_SPEC' s_raw.log)
    cp s_raw.log "s_${mode:-default}.log"
done

grep ISLAND_SPEC s_default.log
grep -q '^250 sum=00 y=xxxxxxxx' off.log
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=66 promoted=66 ' s_default.log
grep -q '^ISLAND_SPEC_RT: cost .* hold_waits=[1-9]' s_default.log
# Fewer island visits than two per island although there were many sweeps
visits=$(sed -n 's/^ISLAND_SPEC_RT: sweeps=\([0-9]*\) visits=\([0-9]*\) .*/\2/p' \
             s_default.log)
sweeps=$(sed -n 's/^ISLAND_SPEC_RT: sweeps=\([0-9]*\) .*/\1/p' s_default.log)
[ "$sweeps" -ge 5 ]
[ "$visits" -lt 132 ]
# Without NVC_ISLAND_SPEC_START nothing is promoted in so short a run
env -u NVC_ISLAND_SPEC_START NVC_ISLAND_SPEC_STATS=1 nvc --work=w_s -r sweep1 \
    2>&1 | nodebug > s_raw.log
diff -u off.log <(grep -v '^ISLAND_SPEC' s_raw.log)
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=66 promoted=0 ' s_raw.log
