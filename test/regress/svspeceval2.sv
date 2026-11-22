interface svspeceval2_if #(parameter int W = 1) ();
  logic [W-1:0] sig;
endinterface

package svspeceval2_pkg;
  typedef enum bit [1:0] {
    E0,
    E1,
    E2,
    EWidth
  } e_t;

  typedef enum int {
    S0 = 10,
    S1,
    S2,
    S3
  } s_t;

  typedef virtual svspeceval2_if #(EWidth) e_vif_t;
  typedef virtual svspeceval2_if #(S3 - S0 + 1) s_vif_t;

  class db #(type T = int);
    static T stored;
    static bit has_val;
    static function void set(T v);
      stored = v;
      has_val = 1;
    endfunction
    static function T get();
      return stored;
    endfunction
  endclass

  class consumer;
    e_vif_t evif;
    s_vif_t svif;
    function bit fetch();
      evif = db#(e_vif_t)::get();
      svif = db#(s_vif_t)::get();
      return evif != null && svif != null;
    endfunction
  endclass
endpackage

module svspeceval2;
  import svspeceval2_pkg::*;

  svspeceval2_if #(EWidth) u_e ();
  svspeceval2_if #(4) u_s ();

  initial begin
    automatic consumer c = new();
    db#(e_vif_t)::set(u_e);
    db#(virtual svspeceval2_if #(4))::set(u_s);
    if (!c.fetch()) begin
      $display("FAIL: config lookup mismatch");
      $finish;
    end
    u_e.sig = 3'b101;
    u_s.sig = 4'b1010;
    #1;
    if (c.evif.sig !== 3'b101 || c.svif.sig !== 4'b1010) begin
      $display("FAIL: evif.sig=%b svif.sig=%b", c.evif.sig, c.svif.sig);
      $finish;
    end
    $display("PASSED");
  end
endmodule
