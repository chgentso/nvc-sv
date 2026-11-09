// A program declared with a parameter port list
// Expected values: IEEE 1800-2017 24.3 (a program may have parameters like a module), 23.10 (parameter
// overrides), 24.7 (the simulation ends when all programs finish)
program svprogparam1_p #(parameter int N = 3) (input int in, output int o);
  initial begin
    o = N + in;
    #1;
    if (N == 5 ? o != 15 : o != 13) $error("N = %0d o = %0d", N, o);
  end
  final if (N == 5) $display("PASSED");
endprogram

module svprogparam1;
  int o1, o2;
  svprogparam1_p #(5) p1(10, o1);
  svprogparam1_p p2(10, o2);
endmodule
