// Expected values: IEEE 1800-2017 6.8/6.4 (uninitialised 4-state variables are x; 4-state values keep x/z), 10.6.2
// (a forced variable keeps the forced value over procedural assignments and keeps it after release until the next
// assignment), 11.5.1 (bit/part selects), 35 (DPI-C). The uvm_hdl_* return codes (1 = success, 0 = failure or
// absent path) follow the UVM register-layer DPI contract, not 1800-2017.
`ifdef USE_UVM
import uvm_pkg::*;
`else
typedef logic [1023:0] uvm_hdl_data_t;
import "DPI-C" context function int uvm_hdl_check_path(string path);
import "DPI-C" context function int uvm_hdl_deposit(string path, uvm_hdl_data_t value);
import "DPI-C" context function int uvm_hdl_force(string path, uvm_hdl_data_t value);
import "DPI-C" context function int uvm_hdl_release_and_read(string path, inout uvm_hdl_data_t value);
import "DPI-C" context function int uvm_hdl_release(string path);
import "DPI-C" context function int uvm_hdl_read(string path, output uvm_hdl_data_t value);
`endif

`timescale 1ns/1ns
module sub;
  logic [7:0] r;
  logic [15:0] mem [0:3];
  logic [99:0] wide;
  logic bit1;
endmodule

module svhdl1;
  sub u0();
  sub u1();
  uvm_hdl_data_t v;
  int ok;
  int err = 0;
  `define CHKV(what, w, exp) if (ok !== 1 || v[w-1:0] !== exp) begin err++; \
    $error("%s: ok=%0d value=%b", what, ok, v[w-1:0]); end

  initial begin
    if (uvm_hdl_check_path("svhdl1.u0.r") != 1 || uvm_hdl_check_path("svhdl1.u1.wide") != 1 ||
        uvm_hdl_check_path("svhdl1.u0.nope") != 0) begin err++; $error("check_path"); end
    ok = uvm_hdl_deposit("svhdl1.u0.r", 8'hA5);
    ok &= uvm_hdl_read("svhdl1.u0.r", v);
    `CHKV("r", 8, 8'hA5)
    ok = uvm_hdl_read("svhdl1.u0.r[3:0]", v);
    `CHKV("r[3:0]", 4, 4'h5)
    ok = uvm_hdl_read("svhdl1.u0.r[7]", v);
    `CHKV("r[7]", 1, 1'b1)
    ok = uvm_hdl_deposit("svhdl1.u0.mem[2]", 16'hBEEF);
    ok &= uvm_hdl_read("svhdl1.u0.mem[2]", v);
    `CHKV("mem[2]", 16, 16'hBEEF)
    ok = uvm_hdl_read("svhdl1.u0.mem[1]", v);
    `CHKV("mem[1]", 16, 16'hxxxx)
    ok = uvm_hdl_deposit("svhdl1.u1.r", 8'bxz01_xz01);
    ok &= uvm_hdl_read("svhdl1.u1.r", v);
    `CHKV("u1.r", 8, 8'bxz01_xz01)
    ok = uvm_hdl_deposit("svhdl1.u1.wide", 100'hf_1234_5678_9abc_def0_1111_2222);
    ok &= uvm_hdl_read("svhdl1.u1.wide", v);
    `CHKV("wide", 100, 100'hf_1234_5678_9abc_def0_1111_2222)
    ok = uvm_hdl_deposit("svhdl1.u0.r[5:4]", 2'b01);
    ok &= uvm_hdl_read("svhdl1.u0.r", v);
    `CHKV("r part", 8, 8'h95)
    ok = uvm_hdl_force("svhdl1.u0.r", 8'h3c);
    #1;
    u0.r = 8'hff;
    #1;
    ok &= uvm_hdl_read("svhdl1.u0.r", v);
    `CHKV("forced", 8, 8'h3c)
    v = 0;
    ok = uvm_hdl_release_and_read("svhdl1.u0.r", v);
    `CHKV("released", 8, 8'h3c)
    #1;
    u0.r = 8'h11;
    #1;
    ok = uvm_hdl_read("svhdl1.u0.r", v);
    `CHKV("assigned", 8, 8'h11)
    ok = uvm_hdl_release("svhdl1.u0.r");
    if (ok !== 1) begin err++; $error("release ok=%0d", ok); end
    if (err == 0) $display("PASSED");
  end
endmodule
