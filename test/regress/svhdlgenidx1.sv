// Self-checking. Expected values: IEEE 1800-2017 27.4 (the blocks of a
// generate loop are named with the value of the genvar, gen[0]), 23.6
// (hierarchical names through them) and the HDL access routines used by UVM
// (uvm_hdl_check_path, uvm_hdl_force, uvm_hdl_read, uvm_hdl_release) which take
// such a name as a string.
module svhdlgenidx1_leaf #(parameter bit Enable = 1);
  if (Enable) begin : gen_enable
    bit flag;
  end
endmodule

module svhdlgenidx1_mid;
  for (genvar i = 0; i < 2; i++) begin : gen_tx
    svhdlgenidx1_leaf #(.Enable(1)) u_leaf();
  end
endmodule

module svhdlgenidx1;
  import "DPI-C" context function int uvm_hdl_check_path(string path);
  import "DPI-C" context function int uvm_hdl_force(string path, input logic [31:0] v);
  import "DPI-C" context function int uvm_hdl_read(string path, output logic [31:0] v);
  import "DPI-C" context function int uvm_hdl_release(string path);
  svhdlgenidx1_mid dut();
  int errs = 0;
  logic [31:0] v;
  initial begin
    #1;
    if (!uvm_hdl_check_path("svhdlgenidx1.dut.gen_tx[0].u_leaf.gen_enable.flag")) begin
      errs++; $error("path with index 0 not found");
    end
    if (!uvm_hdl_check_path("svhdlgenidx1.dut.gen_tx[1].u_leaf.gen_enable.flag")) begin
      errs++; $error("path with index 1 not found");
    end
    if (uvm_hdl_check_path("svhdlgenidx1.dut.gen_tx[2].u_leaf.gen_enable.flag")) begin
      errs++; $error("path with index 2 found");
    end
    if (!uvm_hdl_force("svhdlgenidx1.dut.gen_tx[1].u_leaf.gen_enable.flag", 1)) begin
      errs++; $error("force failed");
    end
    #1;
    if (dut.gen_tx[1].u_leaf.gen_enable.flag !== 1'b1 || dut.gen_tx[0].u_leaf.gen_enable.flag !== 1'b0) begin
      errs++; $error("forced value not seen");
    end
    if (!uvm_hdl_read("svhdlgenidx1.dut.gen_tx[1].u_leaf.gen_enable.flag", v) || v[0] !== 1'b1) begin
      errs++; $error("read failed");
    end
    void'(uvm_hdl_release("svhdlgenidx1.dut.gen_tx[1].u_leaf.gen_enable.flag"));
    if (errs == 0) $display("PASSED");
  end
endmodule
