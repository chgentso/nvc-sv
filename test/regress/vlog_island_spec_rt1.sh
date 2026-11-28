set -xe

# Speculative two-state variants of SV islands at run time (NVC_ISLAND_SPEC=1):
# an island runs its four-state functions from time zero and switches to
# "$a" (unknown bits taken as zero) once every four-state value in its
# state is 0/1, or to "$ah" (loads check the unknown bits) when some are
# not.  Covered: a register left x that feeds comb logic, an output port,
# another island and an unfused process, then written; division by zero,
# x and z literals, a select out of range and an x stored by a flip-flop
# (deopt before the store); x and z arriving on inputs; ===, ==?, casez
# and $isunknown; a memory whose rows are read before they are written;
# a force and release; a register assigned twice in a time step whose
# change and change back an unfused process counts.  The output and the
# waveform must be those of --rtl-islands=off in every mode.

cat >spec1.sv <<'EOF'

module xu (input logic clk, wen, input logic [7:0] d,
           output logic [7:0] q, y, s);
  logic [7:0] ru;                  // No reset: x until the first write
  always_ff @(posedge clk) if (wen) ru <= d;
  assign q = ru;
  assign y = (ru & d) | (ru ^ d);
  assign s = ru + d;
endmodule

module down (input logic clk, input logic [7:0] a,
             output logic [7:0] q, output logic p);
  always_ff @(posedge clk) begin
    q <= a + 8'd1;
    p <= ^a;
  end
endmodule

module dvm (input logic [7:0] a, b, output logic [7:0] dv, md);
  always_comb begin
    dv = a / b;                    // Division by zero gives x
    md = a % b;
  end
endmodule

module xlit (input logic [7:0] a, b, input logic [1:0] sel,
             output logic [7:0] cx, cz);
  always_comb begin
    case (sel)
      2'b00:   cx = a & b;
      2'b01:   cx = a | b;
      2'b10:   cx = a ^ b;
      default: cx = 8'bz;
    endcase
  end
  always_comb begin
    casez (sel)
      2'b1?:   cz = a;
      2'b01:   cz = b;
      default: cz = 'x;
    endcase
  end
endmodule

module oor (input logic clk, rst, input logic [3:0] idx,
            input logic [7:0] b, output logic [7:0] rd);
  logic [7:0] arr [0:7];
  always_ff @(posedge clk)
    if (rst) begin
      for (int i = 0; i < 8; i++) arr[i] <= i;
    end
    else
      arr[idx[2:0]] <= b;
  always_comb rd = arr[idx];       // Out of range for idx > 7
endmodule

module ffx (input logic clk, input logic [7:0] a, b,
            output logic [7:0] dq);
  always_ff @(posedge clk)
    dq <= (b == 8'd7) ? 8'bx : b + (a & 8'h3);   // x stored by a flip-flop
endmodule

module cmp (input logic clk, rst, input logic [7:0] a, b,
            output logic [7:0] ce, uk, cs);
  logic [7:0] r;
  always_ff @(posedge clk or posedge rst)
    if (rst) r <= 0;
    else r <= r + a;
  always_comb ce = (a === 8'bx) ? 8'h11 : ((b !== 8'h00) ? 8'h22 : 8'h33);
  always_comb uk = $isunknown(r) ? 8'hee : r;
  always_comb begin
    casez (a)
      8'b1???_???1: cs = 8'h01;
      8'b01??_????: cs = 8'h02;
      default:      cs = b ==? 8'b1x1x_0z0z ? 8'h03 : 8'h04;
    endcase
  end
endmodule

module memu (input logic clk, input logic [3:0] idx, input logic [7:0] b,
             output logic [7:0] rd);
  logic [7:0] mem [0:15];          // Rows read before they are written
  always_ff @(posedge clk) if (b[0]) mem[idx] <= b;
  assign rd = mem[idx ^ 4'h5];
endmodule

module gl (input logic clk, v, output logic q);
  always_ff @(posedge clk) begin   // Changes and changes back
    q <= ~v;
    q <= v;
  end
endmodule

module spec1;
  logic clk = 0, rst = 0, wen = 0, v = 0;
  logic [7:0] d = 0, a = 0, b = 0;
  logic [3:0] idx = 0;
  logic [1:0] sel = 0;
  wire [7:0] u_q, u_y, u_s, dn_q, dv, md, cx, cz, rd, dq, ce, uk, cs, mrd;
  wire dn_p, g_q;
  int nq = 0;

  xu u (.clk, .wen, .d, .q(u_q), .y(u_y), .s(u_s));
  down dn (.clk, .a(u_q), .q(dn_q), .p(dn_p));
  dvm dvm (.a, .b, .dv, .md);
  xlit xl (.a, .b, .sel, .cx, .cz);
  oor oo (.clk, .rst, .idx, .b, .rd);
  ffx fx (.clk, .a, .b, .dq);
  cmp cm (.clk, .rst, .a, .b, .ce, .uk, .cs);
  memu mm (.clk, .idx, .b, .rd(mrd));
  gl g (.clk, .v, .q(g_q));

  always @(g_q) nq++;              // Not in an island

  initial begin
    #1 rst = 1;
    #1 clk = 1;
    #1 clk = 0;
    #1 rst = 0;
    for (int k = 0; k < 60; k++) begin
      d = (k * 53 + 29) % 256;
      a = (k * 37 + 3) % 256;
      b = (k % 23 == 20) ? 0 : (k % 17 == 9) ? 7 : (k * 11 + 5) % 256;
      if (k == 42) b = 8'hxx;
      if (k == 47) a = 8'bz0z0z0z0;
      idx = (k % 19 == 11) ? 4'd12 : (k * 5) % 8;
      sel = (k % 13 == 7) ? 2'b11 : k % 3;
      wen = (k == 12 || k == 13 || k == 50);
      v = k[0];
      if (k == 34) force u.ru = 8'h5a;
      if (k == 36) release u.ru;
      #1 clk = 1;
      #1 clk = 0;
      $display("%0d u=%b y=%b s=%h dn=%h %b dv=%h %h x=%h %h rd=%h dq=%h ",
               k, u_q, u_y, u_s, dn_q, dn_p, dv, md, cx, cz, rd, dq,
               "c=%h %h %h m=%h g=%b nq=%0d", ce, uk, cs, mrd, g_q, nq);
    end
    $finish;
  end
endmodule
EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT NVC_ISLAND_SPEC_HOLD_WAIT
unset NVC_RTL_ISLANDS

nd() { sed '/^\$date/,/\$end/d' $1; }
nodebug() { grep -v '^\*\* Debug' || true; }   # Debug builds: JIT footprint

nvc --work=w_off -a spec1.sv
nvc --work=w_off --rtl-islands=off -e spec1 -r --wave=off.vcd --format=vcd \
    2>&1 | nodebug > off.log

export NVC_ISLAND_SPEC=1
nvc --work=w_s -a spec1.sv
nvc --work=w_s -e spec1
for mode in "" NVC_ISLAND_SPEC_FORCE=A NVC_ISLAND_SPEC_STRESS=3 \
            NVC_ISLAND_SPEC_HOLD_WAIT=0 NVC_ISLAND_ACT_CHECK=1; do
    env $mode NVC_ISLAND_SPEC_STATS=1 nvc --work=w_s -r --wave=s.vcd \
        --format=vcd spec1 2>&1 | nodebug > s_raw.log
    grep -v '^ISLAND_SPEC' s_raw.log > s.log
    diff -u off.log s.log
    diff -u <(nd off.vcd) <(nd s.vcd)
    cp s_raw.log "s_${mode:-default}.log"
done
NVC_ISLAND_SPEC_STATS=1 nvc --work=w_s -r --no-island-fast spec1 2>&1 \
    | nodebug > nf_raw.log
diff -u off.log <(grep -v '^ISLAND_SPEC' nf_raw.log)

grep ISLAND_SPEC s_default.log s_NVC_ISLAND_SPEC_FORCE=A.log
# The x register reaches comb logic, the next island and the monitor
grep -q '^0 u=xxxxxxxx y=xxxxxxxx s=xx dn=xx x ' off.log
grep -q '^13 u=11001110 y=11001110 ' off.log
# The variants ran: islands were promoted and deopted before storing x;
# with FORCE=A every island ran a variant and "$ah" read an unknown value;
# the forced island was pinned
grep -q '^ISLAND_SPEC_RT: deopts .* xstore=[1-9]' s_default.log
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=9 promoted=9 ' \
     s_NVC_ISLAND_SPEC_FORCE=A.log
grep -q '^ISLAND_SPEC_RT: deopts .* unclean=[1-9][0-9]* .*xstore=[1-9]' \
     s_NVC_ISLAND_SPEC_FORCE=A.log
grep -q '^ISLAND_SPEC_RT: pinned .* put=1 ' s_default.log
