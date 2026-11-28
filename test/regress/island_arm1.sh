set -xe

# A fused always @(...) block runs only after an event on its event control,
# as without islands: not at time zero without an event (also when the island
# runs for another block or input), after an event that a continuously driven
# input or net has at time zero when driven to a 0 or 1 bit, after a variable
# assigned twice by a process outside the island changes and changes back,
# and after a net goes from x to z.  Output and waves must be the same as
# NVC_RTL_ISLANDS=off.

cat >arm1.sv <<'EOF'
module arm1_sel (input [3:0] a, input clk, output reg y, output reg [3:0] q);
  wire s0 = (a >= 4'd2);
  wire s1 = (a < 4'd8);
  integer k;
  reg z;
  always @(s0 or s1)            // No event before a changes
    case ({s1, s0})
      2'b01: y = 1'b1;
      default: y = 1'b0;
    endcase
  always @(a) for (k = 0; k < 4; k = k + 1) z = a[k];
  always @(posedge clk) q <= a;
endmodule

module arm1_cnt (input [1:0] a, input [1:0] b, output reg y1, output reg y2,
                 output reg y3);
  always @* y1 = (a == 2'b00) ? 1'b1 : 1'b0;
  always @(a) y2 = (a == 2'b00) ? 1'b1 : 1'b0;
  always_comb y3 = (b == 2'b00) ? 1'b1 : 1'b0;   // Runs at time zero
endmodule

module arm1_glitch (input [1:0] x, output reg [1:0] y);
  always @(x) if (x[0] == 1'b1) y = 2'b11; else y = 2'b00;
endmodule

module arm1_init (input a, input [1:0] b, input c, output reg y, output reg z,
                  output reg w);
  wire t = c & 1'b0;            // Driven to 0 when the simulation starts
  always @(a) y = ~a;
  always @(b[0]) z = ~b[0];
  always @(t) w = t ? 1'b0 : 1'b1;
endmodule

module arm1_tri (input oe, input d, output reg [1:0] y);
  wire p = oe ? d : 1'bz;
  always @(p) y = {p === 1'bz, 1'b1};
endmodule

module arm1;
  reg [3:0] a;
  reg clk = 0;
  wire y; wire [3:0] q;
  arm1_sel u_sel (.a(a), .clk(clk), .y(y), .q(q));

  reg [1:0] ca, cb;
  wire y1, y2, y3;
  arm1_cnt u_cnt (.a(ca), .b(cb), .y1(y1), .y2(y2), .y3(y3));

  reg [1:0] gx, gb;
  wire [1:0] gy;
  always @(gb) begin            // Changes gx and changes it back
    gx = 2'b00;
    gx = 2'bxx;
  end
  arm1_glitch u_glitch (.x(gx), .y(gy));

  reg ia = 0;
  reg [1:0] ib = 2'b10;
  reg ic;
  wire iy, iz, iw;
  arm1_init u_init (.a(ia), .b(ib), .c(ic), .y(iy), .z(iz), .w(iw));

  reg oe, d;
  wire [1:0] ty;
  arm1_tri u_tri (.oe(oe), .d(d), .y(ty));

  initial begin
    #1 $display("%0t y=%b k=%0d y1=%b y2=%b y3=%b gy=%b init=%b%b%b ty=%b",
                $time, y, u_sel.k, y1, y2, y3, gy, iy, iz, iw, ty);
    cb = 2'b01;                 // Runs arm1_cnt's island only
    gb = 2'b01;
    oe = 1; d = 0;
    #1 $display("%0t y=%b y1=%b y2=%b y3=%b gy=%b ty=%b", $time, y, y1, y2,
                y3, gy, ty);
    a = 3; ca = 2'b00;
    oe = 0;
    #1 clk = 1;
    #1 $display("%0t y=%b q=%b k=%0d y1=%b y2=%b ty=%b", $time, y, q,
                u_sel.k, y1, y2, ty);
    $finish;
  end
endmodule
EOF

strip_date() {
   sed '/^\$date/,/^\$end/d' "$1" > "$2"
}

run_mode() {
   mkdir -p $1
   ( cd $1
     export NVC_RTL_ISLANDS=$2 NVC_ISLAND_FAST=$3
     nvc -a ../arm1.sv -e arm1
     nvc -r arm1 >run.out
     nvc -r --wave=w.vcd --format=vcd arm1 >wave.out
     strip_date w.vcd w.norm )
}

run_mode fast hier 1
run_mode nofast hier 0
run_mode off off 0

cat off/run.out
grep -q '^1 y=x k=x y1=x y2=x y3=x gy=xx init=111 ty=xx$' off/run.out

for m in fast nofast; do
   diff -u off/run.out $m/run.out
   diff -u off/run.out $m/wave.out
   diff -u off/w.norm $m/w.norm
done
