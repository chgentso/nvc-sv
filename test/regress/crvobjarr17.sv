// An array of more than 16 rand objects with an ordering constraint
// between neighbours is satisfiable
module crvobjarr17;
  class item;
    rand bit [31:0] addr;
    rand bit [7:0]  len;
    constraint c { addr[1:0] == 0; len inside {[1:16]}; }
  endclass
  class txn;
    rand item it[];
    constraint c { it.size() == 24;
                   foreach (it[i]) if (i > 0) it[i].addr > it[i-1].addr; }
    function void pre_randomize();
      if (it.size() == 0) begin it = new[24]; foreach (it[i]) it[i] = new; end
    endfunction
  endclass
  initial begin
    automatic txn t = new;
    repeat (5) begin
      if (!t.randomize()) $fatal(1, "randomize failed");
      foreach (t.it[i]) begin
        if (t.it[i].addr[1:0] != 0 || t.it[i].len < 1 || t.it[i].len > 16)
          $fatal(1, "item %0d out of range", i);
        if (i > 0 && t.it[i].addr <= t.it[i-1].addr)
          $fatal(1, "item %0d not ordered", i);
      end
    end
    $display("PASSED");
  end
endmodule
