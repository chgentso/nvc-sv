// Replication in assignment patterns of nested and packed types
// Expected values: IEEE 1800-2017 10.9.1 (replication in array patterns), 10.9.2 (structure patterns),
// 5.11 (the leftmost positional item of a packed array pattern is the most significant element)
module svpatrep2;
  struct {int X,Y,Z;} XYZ = '{3{1}};
  typedef struct {int a,b[4];} ab_t;
  int a,b,c;
  ab_t v1[1:0] [2:0];
  struct packed {logic [3:0] p; logic [3:0] q;} pq = '{2{4'h3}};
  logic [3:0][1:0] pa = '{4{2'b10}};
  bit [3:0] pb = '{1'b1, 1'b0, 1'b1, 1'b1};
  int errs = 0;
  initial begin
    a = 1; b = 2; c = 3;
    v1 = '{2{'{3{'{a,'{2{b,c}}}}}}};
    if (XYZ.X !== 1 || XYZ.Y !== 1 || XYZ.Z !== 1) begin $error("A"); errs++; end
    if (v1[0][0].a !== 1 || v1[0][0].b[0] !== 2 || v1[0][0].b[1] !== 3 || v1[0][0].b[2] !== 2 || v1[0][0].b[3] !== 3) begin
      $error("B"); errs++;
    end
    if (v1[1][2].a !== 1 || v1[1][2].b[3] !== 3) begin $error("C"); errs++; end
    if (pq !== 8'h33 || pa !== 8'hAA || pb !== 4'b1011) begin $error("D %h %h %b", pq, pa, pb); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
