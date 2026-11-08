// A default value in an assignment pattern for an array with more than one dimension
// Expected values: IEEE 1800-2017 10.9.1 (array assignment patterns: default and index keys; nested patterns)
module svpatdef1;
  int m [2][2] = '{default:5};
  int n [2][2];
  int errs = 0;
  initial begin
    n = '{default:3};
    if (m[0][0] !== 5 || m[1][1] !== 5 || m[0][1] !== 5 || m[1][0] !== 5) begin $error("m default"); errs++; end
    if (n[0][1] !== 3 || n[1][0] !== 3 || n[0][0] !== 3 || n[1][1] !== 3) begin $error("n default"); errs++; end
    n = '{0:'{default:1}, 1:'{7, 8}};
    if (n[0][0] !== 1 || n[0][1] !== 1 || n[1][0] !== 7 || n[1][1] !== 8) begin
      $error("B %0d %0d %0d %0d", n[0][0], n[0][1], n[1][0], n[1][1]); errs++;
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
