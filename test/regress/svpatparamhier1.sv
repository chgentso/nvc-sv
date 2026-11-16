// Hierarchical reference to a parameter whose value is an assignment pattern, used as a system task argument
// Expected values: IEEE 1800-2017 10.9.2 (structure patterns), 6.20 (parameter overrides)
typedef struct packed { logic s; logic [2:0] m; } st_t;

module svpatparamhier1_sub #(parameter st_t P = '{s: 1, m: 3}) ();
endmodule

module svpatparamhier1;
  svpatparamhier1_sub #(.P('{s: 0, m: 5})) u1 ();
  svpatparamhier1_sub u2 ();
  initial begin
    string a, b;
    a = $sformatf("%b", u1.P);
    b = $sformatf("%b", u2.P);
    if (a == "0101" && b == "1011")
      $display("PASSED");
    else
      $error("param pattern: %s %s", a, b);
  end
endmodule
