set -xe

# Speculative variant of an SV island with a memory that stays partly x
# (NVC_ISLAND_SPEC=1): the island is promoted holding the x rows ("$ah")
# and its flip-flop function deopts when it reads a register that is never
# written, after it stored to the memory in the same call.  The deopt
# undoes only the logged memory element, and the checks for unknown bits
# start where the last one was found instead of scanning the memory.
# Output must be that of --rtl-islands=off in every mode.

cat >specmem1.sv <<'SV_EOF'
module specmem1_ram (input logic clk, we, sel, input logic [9:0] wa, ra,
                     input logic [15:0] d, output logic [15:0] q,
                     output logic [15:0] r);
  logic [15:0] mem [0:1023];       // x until written
  logic [15:0] ru;                 // Never written
  always_ff @(posedge clk) begin
    if (we) mem[wa] <= d;          // Stored before the x is read
    if (sel) r <= ru ^ d;
    else r <= d + 16'd1;
  end
  assign q = mem[ra];
endmodule

module specmem1;
  logic clk = 0, we = 0, sel = 0;
  logic [9:0] wa = 0, ra = 0;
  logic [15:0] d = 0;
  wire [15:0] q, r;

  specmem1_ram u (.clk, .we, .sel, .wa, .ra, .d, .q, .r);

  initial begin
    for (int k = 0; k < 400; k++) begin
      we = 1;
      ra = wa;
      wa = (k * 7) % 64;
      d = (k * 4099 + 17) % 65536;
      sel = (k % 37 == 20) || (k % 53 == 30);
      #1 clk = 1;
      #1 clk = 0;
      if (k % 20 == 0 || sel) $display("%0d q=%h r=%h", k, q, r);
    end
    $finish;
  end
endmodule
SV_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_HOLD_WAIT NVC_RTL_ISLANDS

nodebug() { grep -v '^\*\* Debug' || true; }   # Debug builds: JIT footprint

nvc --work=w_off -a specmem1.sv
nvc --work=w_off --rtl-islands=off -e specmem1 -r 2>&1 | nodebug > off.log

export NVC_ISLAND_SPEC=1
nvc --work=w_s -a specmem1.sv
nvc --work=w_s -e specmem1
for mode in "" NVC_ISLAND_SPEC_HOLD_WAIT=0 NVC_ISLAND_SPEC_FORCE=A \
            NVC_ISLAND_SPEC_STRESS=3; do
    env $mode NVC_ISLAND_SPEC_STATS=1 nvc --work=w_s -r specmem1 2>&1 \
        | nodebug > s_raw.log
    diff -u off.log <(grep -v '^ISLAND_SPEC' s_raw.log)
    cp s_raw.log "s_${mode:-default}.log"
done

grep ISLAND_SPEC s_default.log s_NVC_ISLAND_SPEC_HOLD_WAIT=0.log
grep -q '^30 q=d068 r=xxxx' off.log
# Promoted holding the x rows, then deopts in the flip-flop function
for f in s_default.log s_NVC_ISLAND_SPEC_HOLD_WAIT=0.log; do
    grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=1 promoted=1 ' $f
    grep -q '^ISLAND_SPEC_RT: deopts .* unclean=[1-9]' $f
done
