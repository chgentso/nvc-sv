// A constant %p of a queue, dynamic array or associative array parameter
// is the same as at run time
module vlogconstfmt3;
  parameter int Q[$] = '{1, 2};
  parameter string QS = $sformatf(">%p<", Q);
  parameter int D[] = '{1, 2};
  parameter string DS = $sformatf(">%p<", D);
  parameter int A[int] = '{1: 10, 2: 20};
  parameter string AS = $sformatf(">%p<", A);
  parameter string SA[string] = '{"a": "x", "b": "y"};
  parameter string SAS = $sformatf("%0p", SA);
  localparam int LD[] = '{3, 4, 5};
  localparam string LS = $sformatf("%p", LD);
  int rq[$] = Q;
  int rd[] = D;
  int ra[int] = A;
  initial begin
    $display("%s %s %s %s %s", QS, DS, AS, SAS, LS);
    $display("%p %p %p", rq, rd, ra);
  end
endmodule
