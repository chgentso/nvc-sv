package vlog_class_param_dim1_pkg;
  class router #(
    parameter int unsigned NoRules,
    parameter type         rule_t,
    parameter rule_t [NoRules-1:0] AddrMap
  );
    function int unsigned lookup(input logic [15:0] addr);
      for (int unsigned j = 0; j < NoRules; j++) begin
        if (addr >= AddrMap[j].start_addr && addr < AddrMap[j].end_addr)
          return AddrMap[j].idx;
      end
      return 99;
    endfunction
  endclass
endpackage

module vlog_class_param_dim1;
  typedef struct packed {
    logic [7:0]  idx;
    logic [15:0] start_addr;
    logic [15:0] end_addr;
  } my_rule_t;

  localparam my_rule_t [1:0] MAP = '{
    '{idx: 8'd1, start_addr: 16'h1000, end_addr: 16'h2000},
    '{idx: 8'd2, start_addr: 16'h2000, end_addr: 16'h3000}
  };

  typedef vlog_class_param_dim1_pkg::router #(
    .NoRules(2),
    .rule_t(my_rule_t),
    .AddrMap(MAP)
  ) my_router_t;

  initial begin
    my_router_t r = new;
    if (r.lookup(16'h1234) !== 1 || r.lookup(16'h2500) !== 2 || r.lookup(16'h0100) !== 99) begin
      $display("FAILED: %0d %0d %0d",
               r.lookup(16'h1234), r.lookup(16'h2500), r.lookup(16'h0100));
      $finish;
    end
    $display("PASSED");
  end
endmodule
