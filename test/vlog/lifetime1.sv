// IEEE 1800-2017 6.21: a variable declared with an initial value in a
// static task, function or procedural block must be declared static or
// automatic explicitly.  nvc warns about each declaration below without
// a lifetime; the ones with a lifetime and the loop variable are legal.
// The arguments of a static subroutine are static variables, so a static
// variable may be initialised from one; the arguments of an automatic
// subroutine or method do not exist before time 0, when static variables
// are initialised, so a static initialiser cannot refer to them.
module lifetime1;
  logic clk;
  task t;
    int a = 0;                  // Warning
    static int s = 0;
    automatic int u = 0;
    int n;                      // No initial value
  endtask

  function automatic int f;
    int x = 1;                  // Automatic function
    return x;
  endfunction

  initial begin
    int k = 2;                  // Warning
    for (int i = 0; i < 2; i++) begin
      int loop3 = 0;            // Warning
      automatic int ok = 0;
    end
  end

  always @(clk) begin
    logic [3:0] v = 0, w = 1;   // Two warnings
  end

  task t2(input int arg);
    static int y = arg;         // OK: arg is static
  endtask

  class c;
    function void m;
      int z = 1;                // A method is automatic
    endfunction
    function void m2(int p);
      static int r = p;         // Error
    endfunction
  endclass

  function automatic void f2(int a);
    static int q = a + 1;       // Error
  endfunction
endmodule
