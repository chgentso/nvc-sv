// The built-in implementation of the DPI routines which UVM declares for its
// HDL back-door access (uvm_hdl.svh), declared here in the same way: no
// foreign library is needed.
module svuvmhdl1;
  typedef logic [1023:0] uvm_hdl_data_t;
  import "DPI-C" context function int uvm_hdl_check_path(string path);
  import "DPI-C" context function int uvm_hdl_deposit(string path, uvm_hdl_data_t value);
  import "DPI-C" context function int uvm_hdl_force(string path, uvm_hdl_data_t value);
  import "DPI-C" context function int uvm_hdl_release_and_read(string path, inout uvm_hdl_data_t value);
  import "DPI-C" context function int uvm_hdl_release(string path);
  import "DPI-C" context function int uvm_hdl_read(string path, output uvm_hdl_data_t value);

  logic [7:0] sig;

  initial begin
    uvm_hdl_data_t v;
    int r;
    r = uvm_hdl_check_path("svuvmhdl1.sig"); $display("check %0d", r);
    r = uvm_hdl_deposit("svuvmhdl1.sig", 8'h5a);
    $display("deposit %0d sig=%h", r, sig);
    r = uvm_hdl_read("svuvmhdl1.sig", v); $display("read %0d %h", r, v[7:0]);
    r = uvm_hdl_force("svuvmhdl1.sig", 8'h11);
    #1 $display("force %0d sig=%h", r, sig);
    r = uvm_hdl_release("svuvmhdl1.sig"); $display("release %0d", r);
    #1 sig = 8'h22;
    #1 $display("sig=%h", sig);
    r = uvm_hdl_check_path("svuvmhdl1.nosuch"); $display("check bad %0d", r);
  end
endmodule
