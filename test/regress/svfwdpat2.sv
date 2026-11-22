package svfwdpat2_pkg;
  typedef struct packed {
    logic [7:0] hi;
    logic [7:0] lo;
  } word_t;
endpackage

interface svfwdpat2_if #(parameter int Tag = 0) ();
  function automatic void apply();
    force u_sub.reg_q = '{hi: 8'hDE, lo: 8'hAD};
  endfunction
endinterface

module svfwdpat2_sub;
  import svfwdpat2_pkg::*;
  word_t reg_q = '0;
endmodule

module svfwdpat2;
  svfwdpat2_sub u_sub();
  svfwdpat2_if #(.Tag(1)) u_if();

  initial begin
    u_if.apply();
    #1;
    if (u_sub.reg_q !== 16'hDEAD)
      $fatal(1, "FAIL: reg_q=%h", u_sub.reg_q);
    $display("PASSED");
  end
endmodule
