interface svspeceval1_if #(parameter int Width = 1);
  logic [Width-1:0] pins;
endinterface

package svspeceval1_pkg;
  typedef enum {
    PinA,
    PinB,
    PinWidth
  } pin_e;

  typedef virtual svspeceval1_if #(PinWidth) my_vif_t;

  class db_c #(type T = int);
    static T stored;
    static function void set(T v);
      stored = v;
    endfunction
    static function T get();
      return stored;
    endfunction
  endclass

  class cfg_c #(parameter int Depth = 10);
    int tag;
  endclass

  class env_c #(parameter int Depth = 10);
    typedef cfg_c #(Depth) my_cfg_t;
    function my_cfg_t fetch_cfg();
      return db_c#(my_cfg_t)::get();
    endfunction
    function my_vif_t fetch_vif();
      return db_c#(my_vif_t)::get();
    endfunction
  endclass
endpackage

module svspeceval1;
  import svspeceval1_pkg::*;

  svspeceval1_if #(PinWidth) u_if();

  initial begin
    automatic cfg_c #(2**10 / 4) c = new();
    automatic env_c #(256) e = new();
    automatic cfg_c #(256) got_c;
    automatic my_vif_t got_v;

    c.tag = 77;
    u_if.pins = 2'b10;
    db_c#(cfg_c #(2**10 / 4))::set(c);
    db_c#(my_vif_t)::set(u_if);

    got_c = e.fetch_cfg();
    got_v = e.fetch_vif();
    if (got_c == null || got_c.tag !== 77)
      $fatal(1, "cfg specialization mismatch");
    if (got_v == null || got_v.pins !== 2'b10)
      $fatal(1, "vif specialization mismatch");
    $display("PASSED");
    $finish;
  end
endmodule
