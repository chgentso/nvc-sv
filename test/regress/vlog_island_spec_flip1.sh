set -xe

# A speculative SV island variant (NVC_ISLAND_SPEC=1) whose input bus is x
# every other cycle: "$a" switches to "$ah" when the x arrives and back at
# a recheck once its state is clean.  An island that keeps switching stays
# longer in "$ah" each time instead of switching at every cycle.  Output
# must be that of --rtl-islands=off in every mode.

cat >specflip1.sv <<'SV_EOF'
module specflip1_acc (input logic clk, rst, input logic [7:0] bus,
                      input logic valid, output logic [7:0] sum);
  always_ff @(posedge clk)
    if (rst) sum <= 8'h00;
    else if (valid) sum <= sum + bus;
endmodule

module specflip1;
  logic clk = 0, rst = 0, valid = 0;
  logic [7:0] bus = 0;
  wire [7:0] sum;

  specflip1_acc u (.clk, .rst, .bus, .valid, .sum);

  initial begin
    #1 rst = 1;
    #1 clk = 1;
    #1 clk = 0;
    #1 rst = 0;
    for (int k = 0; k < 600; k++) begin
      valid = k % 2 == 0;
      bus = valid ? (k * 29 + 7) % 256 : 8'hxx;   // x while idle
      #1 clk = 1;
      #1 clk = 0;
      if (k % 100 == 0) $display("%0d sum=%h", k, sum);
    end
    $display("end sum=%h", sum);
    $finish;
  end
endmodule
SV_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_HOLD_WAIT NVC_RTL_ISLANDS
export NVC_ISLAND_SPEC_START=0   # Short run: promote from the start

nodebug() { grep -v '^\*\* Debug' || true; }   # Debug builds: JIT footprint

nvc --work=w_off -a specflip1.sv
nvc --work=w_off --rtl-islands=off -e specflip1 -r 2>&1 | nodebug > off.log

export NVC_ISLAND_SPEC=1
nvc --work=w_s -a specflip1.sv
nvc --work=w_s -e specflip1
for mode in "" NVC_ISLAND_SPEC_FORCE=A NVC_ISLAND_SPEC_STRESS=2; do
    env $mode NVC_ISLAND_SPEC_STATS=1 nvc --work=w_s -r specflip1 2>&1 \
        | nodebug > s_raw.log
    diff -u off.log <(grep -v '^ISLAND_SPEC' s_raw.log)
    cp s_raw.log "s_${mode:-default}.log"
done

grep ISLAND_SPEC s_default.log
grep -q '^end sum=88' off.log
# 300 x arrivals, a few switches to "$ah"
n=$(sed -n 's/^ISLAND_SPEC_RT: sv .* to_checked=\([0-9]*\) .*/\1/p' \
        s_default.log)
[ "$n" -ge 2 ]
[ "$n" -le 20 ]
