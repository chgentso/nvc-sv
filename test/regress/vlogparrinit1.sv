// An unpacked array variable initialised from an array, slice or parameter
typedef struct packed { logic [3:0] a; logic [3:0] b; } ps_t;
typedef struct { int a; logic [7:0] b; } us_t;

module vlogparrinit1;
  parameter int P1 [3] = '{1, 2, 3};
  localparam int N [2][3] = '{'{1, 2, 3}, '{4, 5, 6}};
  localparam logic [7:0] PK [4] = '{8'h11, 8'h22, 8'h33, 8'h44};
  localparam us_t S [2] = '{'{1, 8'h2}, '{3, 8'h4}};
  localparam ps_t PS [2] = '{8'h12, 8'h34};
  localparam string ST [2] = '{"ab", "cde"};
  localparam real R [2] = '{1.5, 2.5};
  int A [2][3] = '{'{7, 8, 9}, '{10, 11, 12}};
  real RA [3] = '{1.5, 2.5, 3.5};

  int rN [2][3] = N;
  int r1 [3] = N[1];
  int rP [3] = P1;
  logic [7:0] rK [4] = PK;
  logic [7:0] rK2 [2] = PK[2:3];
  us_t rS [2] = S;
  ps_t rPS [2] = PS;
  string rST [2] = ST;
  real rR [2] = R;
  int rA [2][3] = A;
  int rA1 [3] = A[1];
  real rRA [3] = RA;
  real rRA2 [2] = RA[1:2];
  int pN [2][3];
  int cN [2][3];
  assign cN = N;

  initial begin
    pN = N;
    #1;
    foreach (rN[i, j]) $display("rN %0d %0d %0d", i, j, rN[i][j]);
    foreach (r1[j]) $display("r1 %0d %0d", j, r1[j]);
    foreach (rP[j]) $display("rP %0d %0d", j, rP[j]);
    foreach (rK[j]) $display("rK %0d %h", j, rK[j]);
    foreach (rK2[j]) $display("rK2 %0d %h", j, rK2[j]);
    foreach (rS[j]) $display("rS %0d %0d %h", j, rS[j].a, rS[j].b);
    foreach (rPS[j]) $display("rPS %0d %h", j, rPS[j]);
    foreach (rST[j]) $display("rST %0d %s", j, rST[j]);
    foreach (rR[j]) $display("rR %0d %f", j, rR[j]);
    foreach (rA[i, j]) $display("rA %0d %0d %0d", i, j, rA[i][j]);
    foreach (rA1[j]) $display("rA1 %0d %0d", j, rA1[j]);
    foreach (rRA[j]) $display("rRA %0d %f", j, rRA[j]);
    foreach (rRA2[j]) $display("rRA2 %0d %f", j, rRA2[j]);
    foreach (pN[i, j]) $display("pN %0d %0d %0d", i, j, pN[i][j]);
    foreach (cN[i, j]) $display("cN %0d %0d %0d", i, j, cN[i][j]);
  end
endmodule
