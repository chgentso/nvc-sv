package svfwdgeneric2_pkg;
  int reg_count = 0;

  function int register_once();
    reg_count++;
    return reg_count;
  endfunction

  typedef class item_c;

  class holder_c #(type T = int);
    T pool[string];
    typedef holder_c #(T) this_type;
    function void copy_from(this_type rhs);
      pool = rhs.pool;
    endfunction
  endclass

  typedef holder_c #(item_c #(int)) item_holder_t;

  class item_c #(type KEY_T = logic [7:0]);
    static int id = register_once();
    KEY_T key;
  endclass
endpackage

module svfwdgeneric2;
  import svfwdgeneric2_pkg::*;

  initial begin
    automatic item_holder_t h1 = new();
    automatic item_holder_t h2 = new();
    automatic item_c #(int) it = new();
    it.key = 42;
    h1.pool["k"] = it;
    h2.copy_from(h1);
    if (h2.pool["k"].key !== 42)
      $fatal(1, "key=%0d exp=42", h2.pool["k"].key);
    if (reg_count !== 1)
      $fatal(1, "reg_count=%0d exp=1 (spurious default specialization)", reg_count);
    $display("PASSED");
  end
endmodule
