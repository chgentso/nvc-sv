// The name of a whole unpacked array is a valid hierarchical path (a
// memory backdoor checks the path of the array before it addresses the
// words), although the array as a whole cannot be read or written. The
// uvm_hdl_* return codes follow the UVM register-layer DPI contract.
import "DPI-C" context function int uvm_hdl_check_path(string path);

module svhdlarr1_mem;
  logic [15:0] mem [0:3];
endmodule

module svhdlarr1;
  if (1) begin : gen_a
    svhdlarr1_mem u_mem();
  end
  initial begin
    if (uvm_hdl_check_path("svhdlarr1.gen_a.u_mem.mem") != 1)
      $fatal(1, "whole array path rejected");
    if (uvm_hdl_check_path("svhdlarr1.gen_a.u_mem.mem[2]") != 1)
      $fatal(1, "array word path rejected");
    if (uvm_hdl_check_path("svhdlarr1.gen_a.u_mem.nope") != 0)
      $fatal(1, "bad path accepted");
    $display("PASSED");
  end
endmodule
