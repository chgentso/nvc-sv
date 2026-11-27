set -xe

# Published direct SV island wires (RT_ISIG_DIRECT_PUB): the island readers
# of a source that also has kernel readers must run in the same delta cycle
# as through the kernel signal, so the deferred unique case reports, the
# displays and the waves are the same with NVC_ISLAND_DIRECT_PUB=0.

cat >dpub2.sv <<'EOF'
module dpub2_src (input logic [1:0] a, output wire [1:0] s);
  assign s = a + 2'd1;
endmodule

module dpub2_rd (input logic [1:0] s, output logic y);
  always_comb begin
    y = 0;
    unique case (s)
      2'd1: y = 1;
      2'd2: y = 0;
    endcase
  end
endmodule

module dpub2;
  logic [1:0] a = 2'd0;
  wire [1:0] s;
  wire y1, y2;

  dpub2_src u_src (.a, .s);
  dpub2_rd u_rd1 (.s, .y(y1));
  dpub2_rd u_rd2 (.s, .y(y2));

  always @(s) $display("%0t s=%d y1=%b y2=%b", $time, s, y1, y2);

  initial begin
    #1 a = 1;
    #1 a = 3;
    #1 a = 2;
    #1 a = 0;
    #1 $finish;
  end
endmodule
EOF

strip_date() {
  sed -e '/^\$date/,/\$end/d' "$1" > "$2"
}

export NVC_RTL_ISLANDS=hier
nvc -a dpub2.sv -e dpub2

NVC_ISLAND_WIRE_STATS=1 nvc -r dpub2 >stats.txt 2>&1
grep -q 'DIRECT+published' stats.txt

nvc -r --island-fast dpub2 --wave=pub.vcd --format=vcd >pub.txt 2>&1
NVC_ISLAND_DIRECT_PUB=0 nvc -r --island-fast dpub2 --wave=nopub.vcd \
  --format=vcd >nopub.txt 2>&1
nvc -r --no-island-fast dpub2 >nofast.txt 2>&1
grep -q '2ns+2: unique case violation' pub.txt

diff -u nopub.txt pub.txt
diff -u nofast.txt pub.txt
strip_date pub.vcd pub.norm
strip_date nopub.vcd nopub.norm
diff -u nopub.norm pub.norm
