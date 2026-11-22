package svfwdpat1_types_pkg;
  typedef struct packed {
    logic [3:0] a;
    logic [3:0] b;
  } pair_t;
endpackage

// Package references virtual interface before interface is declared
package svfwdpat1_pkg;
  import svfwdpat1_types_pkg::*;

  class caller;
    virtual svfwdpat1_if vif;

    function void apply();
      vif.set_pair('{a: 4'hC, b: 4'h3}, 0);
      vif.set_pair('{default: 4'h7}, 1);
    endfunction
  endclass
endpackage

interface svfwdpat1_if ();
  import svfwdpat1_types_pkg::*;
  pair_t pairs[2];

  function automatic void set_pair(pair_t val, int idx);
    pairs[idx] = val;
  endfunction
endinterface

// Forward module instantiation with unpacked array port assignment pattern
module svfwdpat1_holder (
  input logic [3:0] x0,
  input logic [3:0] x1,
  output logic [7:0] sum_o
);
  svfwdpat1_sub u_sub (
    .arr_i('{x0, x1}),
    .sum_o(sum_o)
  );
endmodule

module svfwdpat1_sub (
  input  logic [3:0] arr_i[2],
  output logic [7:0] sum_o
);
  assign sum_o = {arr_i[0], arr_i[1]};
endmodule

module svfwdpat1_target;
  import svfwdpat1_types_pkg::*;
  pair_t cur;
endmodule

module svfwdpat1;
  import svfwdpat1_types_pkg::*;
  import svfwdpat1_pkg::*;

  svfwdpat1_if u_if();
  svfwdpat1_target u_tgt();

  logic [3:0] a = 4'h9, b = 4'h6;
  logic [7:0] sum;
  svfwdpat1_holder u_hold (.x0(a), .x1(b), .sum_o(sum));

  initial begin
    automatic caller c = new();
    c.vif = u_if;
    c.apply();
    force u_tgt.cur = '{a: 4'h5, b: 4'hA};
    #1;
    if (u_if.pairs[0] !== 8'hC3 || u_if.pairs[1] !== 8'h77)
      $fatal(1, "FAIL: vif pairs=%h %h", u_if.pairs[0], u_if.pairs[1]);
    if (sum !== 8'h96)
      $fatal(1, "FAIL: port sum=%h", sum);
    if (u_tgt.cur !== 8'h5A)
      $fatal(1, "FAIL: force cur=%h", u_tgt.cur);
    $display("PASSED");
  end
endmodule
