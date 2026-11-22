package svgenclass1_pkg;
  class pool_c #(type KEY = string, type T = int);
    typedef pool_c #(KEY, T) this_type;
    T map[KEY];

    function void copy_from(this_type rhs);
      map = rhs.map;
    endfunction
  endclass
endpackage

module svgenclass1;
  import svgenclass1_pkg::*;

  localparam bit EN = 1'b0;

  if (EN) begin : gen_dead
    initial begin
      automatic pool_c #(string, logic [7:0]) a = new();
      automatic pool_c #(string, logic [7:0]) b = new();
      a.map["x"] = 8'h42;
      b.copy_from(a);
    end
  end

  initial begin
    automatic pool_c #(string, int) p1 = new();
    automatic pool_c #(string, int) p2 = new();
    p1.map["k"] = 99;
    p2.copy_from(p1);
    if (p2.map["k"] !== 99)
      $fatal(1, "got %0d exp 99", p2.map["k"]);
    $display("PASSED");
  end
endmodule
