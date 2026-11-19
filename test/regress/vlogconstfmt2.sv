// %p of constant aggregates in a constant $sformatf matches the run time result
module vlogconstfmt2;
  typedef enum {RED, GREEN} e_t;
  typedef struct {int f; bit [3:0] s; string n;} s_t;
  typedef struct {int a; int b[2];} u_t;
  localparam int N[2][3] = '{'{1,2,3},'{4,5,6}};
  localparam bit [11:0] W[3] = '{12'h1, 12'h2, 12'hfff};
  localparam string ST[2] = '{"ab", "c"};
  localparam real RR[2] = '{1.5, 2.25};
  localparam e_t EE[2] = '{RED, GREEN};
  localparam s_t S = '{10, 4'h2, "x"};
  localparam s_t SA[2] = '{'{1, 2, "p"}, '{3, 4, "q"}};
  localparam u_t U = '{7, '{8, 9}};
  localparam int I = 234;
  localparam string C = $sformatf("%p|%p|%p|%p|%p|%p|%p|%p|%p|%p", N, W, ST, RR, EE, S, SA, U, I, RR[1]);
  int rN[2][3] = '{'{1,2,3},'{4,5,6}}; bit [11:0] rW[3] = '{12'h1, 12'h2, 12'hfff}; string rST[2] = '{"ab", "c"}; real rRR[2] = '{1.5, 2.25}; e_t rEE[2] = '{RED, GREEN};
  s_t rS = '{10, 4'h2, "x"}; s_t rSA[2] = '{'{1, 2, "p"}, '{3, 4, "q"}}; u_t rU = '{7, '{8, 9}}; int rI = 234;
  localparam string UC = $sformatf("%p%0p", N[1], ST);
  string r;
  initial begin
    r = $sformatf("%p|%p|%p|%p|%p|%p|%p|%p|%p|%p", rN, rW, rST, rRR, rEE, rS, rSA, rU, rI, rRR[1]);
    $display("%s", C); $display("%s", r);
    if (C == r) $display("MATCH"); else $display("DIFF");
    $display("%s", UC);
  end
endmodule
