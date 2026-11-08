// Expected values: IEEE 1800-2017 7.2.1 (packed struct layout: first member in the most significant bits),
// 10.3 (continuous assignment to struct members of a variable), 6.4, 35 (DPI-C). uvm_hdl_* return codes
// (1 = success, 0 = absent path) follow the UVM register-layer DPI contract, not 1800-2017.
typedef logic [1023:0] uvm_hdl_data_t;
import "DPI-C" context function int uvm_hdl_check_path(string path);
import "DPI-C" context function int uvm_hdl_deposit(string path, uvm_hdl_data_t value);
import "DPI-C" context function int uvm_hdl_read(string path, output uvm_hdl_data_t value);

typedef struct packed {
  logic       de;
  logic [3:0] d;
} field_t;
typedef struct packed {
  field_t a;
  field_t b;
  logic [2:0] c;
} reg_t;

module sub;
  reg_t hw2reg;
  assign hw2reg.a.de = 1'b1;
  assign hw2reg.a.d = 4'h9;
  assign hw2reg.b = {1'b0, 4'h6};
  assign hw2reg.c = 3'b101;
endmodule

module svhdl2;
  sub u0();
  uvm_hdl_data_t v;
  int ok;
  int err = 0;
  `define CHKV(what, w, exp) if (ok !== 1 || v[w-1:0] !== exp) begin err++; \
    $error("%s: ok=%0d value=%b", what, ok, v[w-1:0]); end
  initial begin
    #1;
    if (uvm_hdl_check_path("svhdl2.u0.hw2reg.a.de") != 1 || uvm_hdl_check_path("svhdl2.u0.hw2reg.b") != 1 ||
        uvm_hdl_check_path("svhdl2.u0.hw2reg.zz") != 0) begin err++; $error("check_path"); end
    ok = uvm_hdl_read("svhdl2.u0.hw2reg.a.de", v); `CHKV("a.de", 1, 1'b1)
    ok = uvm_hdl_read("svhdl2.u0.hw2reg.a.d", v); `CHKV("a.d", 4, 4'h9)
    ok = uvm_hdl_read("svhdl2.u0.hw2reg.b", v); `CHKV("b", 5, 5'h06)
    ok = uvm_hdl_read("svhdl2.u0.hw2reg.b.d", v); `CHKV("b.d", 4, 4'h6)
    ok = uvm_hdl_read("svhdl2.u0.hw2reg.c", v); `CHKV("c", 3, 3'b101)
    ok = uvm_hdl_read("svhdl2.u0.hw2reg.c[1]", v); `CHKV("c[1]", 1, 1'b0)
    if (err == 0) $display("PASSED");
    $finish;
  end
endmodule
