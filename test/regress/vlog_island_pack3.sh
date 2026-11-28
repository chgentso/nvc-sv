set -xe

# Packed SystemVerilog super-domain sampling and commits
# (NVC_ISLAND_PACK_DOM): many island domains on one clock with plain
# register runs, published outputs, a small memory with an element write
# log, a large sparse memory, a register assigned several values by
# non-blocking assignments in one time step and always @(...) blocks
# waiting for flip-flop outputs, two domains in one island, a force and
# release of a register, with and without waves and speculative variants.
# Output and dump must be the same with NVC_ISLAND_PACK_DOM=0 and with
# --rtl-islands=off.

cat >pack3.sv <<'EOF'
module pack3_unit #(parameter INC = 1) (input clk, input rst_n,
                                        input [3:0] addr,
                                        output logic [7:0] q,
                                        output logic [7:0] rd,
                                        output logic [15:0] wide);
  logic [7:0] mem [0:15];
  logic [7:0] big [0:65535];
  logic [7:0] g;
  logic [7:0] seen;
  logic [127:0] acc;
  logic [7:0] q2;

  always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      q <= 8'h00;
      acc <= 128'h0;
      g <= 8'h00;
    end
    else begin
      q <= q + INC;
      acc <= {acc[119:0], q};
      g <= q;
      if (q[0]) g <= q + 8'd1;   // Overwrites the first value
      mem[q[3:0]] <= q ^ 8'h5a;
      big[{q, q}] <= q;
    end

  // A second process on the same clock in the same island
  always @(posedge clk) q2 <= q + 8'd2;

  always @(g) seen = g;        // Woken by the flip-flop output

  assign rd = mem[addr] + big[{addr, addr}];
  assign wide = acc[15:0] ^ {seen, q2};
endmodule

module pack3;
  logic clk = 0, rst_n = 0;
  logic [3:0] addr = 0;
  wire [7:0] q [1:6];
  wire [7:0] rd [1:6];
  wire [15:0] wide [1:6];

  pack3_unit #(1) u1 (.clk, .rst_n, .addr, .q(q[1]), .rd(rd[1]), .wide(wide[1]));
  pack3_unit #(2) u2 (.clk, .rst_n, .addr, .q(q[2]), .rd(rd[2]), .wide(wide[2]));
  pack3_unit #(3) u3 (.clk, .rst_n, .addr, .q(q[3]), .rd(rd[3]), .wide(wide[3]));
  pack3_unit #(4) u4 (.clk, .rst_n, .addr, .q(q[4]), .rd(rd[4]), .wide(wide[4]));
  pack3_unit #(5) u5 (.clk, .rst_n, .addr, .q(q[5]), .rd(rd[5]), .wide(wide[5]));
  pack3_unit #(6) u6 (.clk, .rst_n, .addr, .q(q[6]), .rd(rd[6]), .wide(wide[6]));

  always #5 clk = ~clk;
  always @(negedge clk) addr <= addr + 4'd3;
  always @(posedge clk)
    $display("%0t q=%h,%h,%h rd=%h,%h wide=%h,%h", $time, q[1], q[2], q[6],
             rd[1], rd[5], wide[2], wide[6]);
  always @(u3.seen) $display("%0t seen3=%h", $time, u3.seen);

  initial begin
    #12 rst_n = 1;
    #100 force u4.q = 8'h33;
    #30 release u4.q;
    #60 $display("end q4=%h rd4=%h wide4=%h", q[4], rd[4], wide[4]);
    $finish;
  end
endmodule
EOF

strip_date() {
  sed -e '/^\$date/,/\$end/d' "$1" > "$2"
}

export NVC_RTL_ISLANDS=hier
export NVC_JIT_THRESHOLD=1 NVC_JIT_ASYNC=0   # Native flip-flop functions
nvc --work=w0 -a pack3.sv -e pack3
NVC_ISLAND_SPEC=1 nvc --work=w1 -a pack3.sv -e pack3
NVC_ISLAND_STATS=1 nvc --work=w0 -r pack3 >stats.txt 2>&1
if [ "${NVC_ISLAND_FAST:-1}" != 0 ]; then   # Fast islands only
  grep -E '^ISLAND_STATS: packed domains: [1-9][0-9.]*% of samples, [1-9][0-9.]*% of commits' stats.txt
fi

run() {
  local tag=$1; shift
  env "$@" nvc --work=$W -r pack3 >$tag.txt 2>&1
  env "$@" nvc --work=$W -r pack3 --wave=pack3.vcd --format=vcd >$tag.w.txt 2>&1
  strip_date pack3.vcd $tag.norm
}

for spec in 0 1; do
  W=w$spec
  run p$spec NVC_ISLAND_SPEC=$spec
  run n$spec NVC_ISLAND_SPEC=$spec NVC_ISLAND_PACK_DOM=0
  run o$spec NVC_ISLAND_SPEC=$spec NVC_RTL_ISLANDS=off
  for x in txt w.txt norm; do
    diff -u n$spec.$x p$spec.$x
    diff -u o$spec.$x p$spec.$x
  done
done
# Different works and runs: debug builds report different code sizes
grep -v '^\*\* Debug:' p0.txt > p0.cmp
grep -v '^\*\* Debug:' p1.txt > p1.cmp
grep -v '^\*\* Debug:' p0.w.txt > p0.w.cmp
diff -u p0.cmp p1.cmp
diff -u p0.cmp p0.w.cmp
grep -q '^end q4=' p0.txt
