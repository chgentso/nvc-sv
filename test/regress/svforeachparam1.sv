// foreach may iterate over the dimensions of a parameter or localparam,
// including one in a package (IEEE 1800-2017 12.7.3). The loop runs from
// the left bound of the dimension to the right bound.
package svforeachparam1_pkg;
  parameter int N = 3;
  parameter logic [N-1:0][1:0] M = {2'd1, 2'd2, 2'd3};
  parameter int A [4] = '{5, 6, 7, 8};
endpackage

module svforeachparam1;
  localparam logic [2:0][1:0] L = {2'd1, 2'd2, 2'd3};
  initial begin
    automatic int n = 0, sum = 0, order = 0;
    foreach (svforeachparam1_pkg::M[i]) begin
      sum += svforeachparam1_pkg::M[i];
      order = order * 10 + i;
      n++;
    end
    if (n != 3 || sum != 6 || order != 210) $fatal(1, "packed package parameter: n=%0d sum=%0d order=%0d", n, sum, order);

    n = 0; sum = 0; order = 0;
    foreach (svforeachparam1_pkg::A[i]) begin
      sum += svforeachparam1_pkg::A[i];
      order = order * 10 + i;
      n++;
    end
    if (n != 4 || sum != 26 || order != 123) $fatal(1, "unpacked package parameter: n=%0d sum=%0d order=%0d", n, sum, order);

    n = 0; sum = 0;
    foreach (L[i]) begin
      sum += L[i];
      n++;
    end
    if (n != 3 || sum != 6) $fatal(1, "localparam: n=%0d sum=%0d", n, sum);
    $display("PASSED");
  end
endmodule
