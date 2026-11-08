// Expected values: IEEE 1800-2017 8.7, 13.4 (function returning an object handle), 23.6/25.? (hierarchical call of an
// interface function through a module instance), 6.21 (static variable initialisers run before procedures start;
// their order relative to the interface's own initialiser is not defined, so acc1/acc2 accept either base value).
class svhiermeth1_Acc; int id; function new(int i); id = i; endfunction endclass
interface svhiermeth1_if;
  int base = 40;
  function automatic svhiermeth1_Acc get_accessor(); svhiermeth1_Acc a = new(base + 2); return a; endfunction
endinterface
module svhiermeth1_sub; svhiermeth1_if cpu(); endmodule
module svhiermeth1;
  svhiermeth1_sub DUT();
  int err = 0;
  initial begin
    static svhiermeth1_Acc acc1 = DUT.cpu.get_accessor();
    automatic svhiermeth1_Acc acc2 = DUT.cpu.get_accessor();
    svhiermeth1_Acc acc3;
    #1;
    acc3 = DUT.cpu.get_accessor();
    if (acc3.id != 42) begin err++; $error("acc3.id=%0d expected 42", acc3.id); end
    if (acc1 == null || (acc1.id != 42 && acc1.id != 2)) begin err++; $error("acc1 bad"); end
    if (acc2 == null || (acc2.id != 42 && acc2.id != 2)) begin err++; $error("acc2 bad"); end
    if (acc1 == acc2 || acc1 == acc3) begin err++; $error("handles not distinct objects"); end
    if (err == 0) $display("PASSED");
  end
endmodule
