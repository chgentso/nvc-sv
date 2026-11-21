typedef enum bit [2:0] { E0, E1 } svtr_e0_t;
typedef enum bit [3:0] { F0, F1 } svtr_e1_t;
typedef struct packed { logic [3:0] a; } svtr_s0_t;
typedef union packed { logic [7:0] a; logic [7:0] b; } svtr_u0_t;
class svtr_C0; bit [7:0] v; endclass
class svtr_C1; bit [15:0] v; endclass

module svtr_M #(
  parameter type enum TE = svtr_e0_t,
  parameter type struct TS = svtr_s0_t,
  parameter type union TU = svtr_u0_t,
  parameter type class TC = svtr_C0
);
  TE e;
  TS s;
  TU u;
  TC c;
endmodule

module svtyperestrict1;
  svtr_M m1();
  svtr_M #(.TE(svtr_e1_t), .TC(svtr_C1)) m2();

  initial begin
    m1.c = new;
    m2.c = new;
    if ($bits(m1.e) !== 3 || $bits(m2.e) !== 4) $fatal(1, "enum mismatch");
    if ($bits(m1.s) !== 4 || $bits(m1.u) !== 8) $fatal(1, "struct/union mismatch");
    if ($bits(m1.c.v) !== 8 || $bits(m2.c.v) !== 16) $fatal(1, "class mismatch");
    $display("PASSED");
  end
endmodule
