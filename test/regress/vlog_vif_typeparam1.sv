typedef struct packed {
  logic [7:0] a;
  logic [7:0] b;
} pair_t;

interface my_if #(parameter type T = pair_t);
  T data;
endinterface

class driver;
  virtual my_if #(pair_t) vif;

  function automatic logic [7:0] read_a();
    return vif.data.a;
  endfunction
endclass

module vlog_vif_typeparam1;
  my_if #(pair_t) u_if();
  driver d;

  initial begin
    d = new();
    d.vif = u_if;
    u_if.data = 16'h5aa5;
    #1;
    if (d.read_a() !== 8'h5a) $fatal(1, "got %h", d.read_a());
    $display("PASSED");
  end
endmodule
