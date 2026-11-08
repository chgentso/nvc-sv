// Expected values: IEEE 1800-2017 13.4 (functions, call without parentheses for a no-argument subroutine, 13.5),
// 25.7 (tasks and functions in interfaces), 23.6 (hierarchical call), 11.4.2 (increment).
interface IA;
  int v = 3;
  function int get(); return v + 1; endfunction
  function void bump(); v++; endfunction
  task automatic tk(); v = v * 2; endtask
endinterface
module svifacecall1;
  IA ia();
  int x;
  int err = 0;
  initial begin
    x = ia.get;
    if (x != 4) begin err++; $error("get=%0d expected 4", x); end
    ia.bump;
    ia.tk;
    if (ia.v != 8) begin err++; $error("v=%0d expected 8", ia.v); end
    if (ia.get() != 9) begin err++; $error("get()=%0d expected 9", ia.get()); end
    x = ia.get + 1;
    if (x != 10) begin err++; $error("get+1=%0d expected 10", x); end
    if (err == 0) $display("PASSED");
  end
endmodule
