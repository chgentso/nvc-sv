// A mask on a variable in a large group of linked variables: aligned
// addresses in ascending order, too many bits for the bdd stage.  Picking
// values that satisfy the constraints on a variable alone is what makes the
// search find them, now that variables are not split at the bits of a mask.
module svcrvunary1;
  class item;
    rand bit [31:0] addr;
    rand bit [7:0]  len;
    rand bit [1:0]  kind;
    constraint c { addr[1:0] == 0; len inside {[1:16]}; kind != 3;
                   (kind == 0) -> len == 1; }
  endclass

  class txn;
    rand item it[];
    rand bit [7:0] total;
    constraint c { it.size() == 100;
                   foreach (it[i]) if (i > 0) it[i].addr > it[i-1].addr; }
    function void pre_randomize();
      if (it.size() == 0) begin
        it = new[100];
        foreach (it[i]) it[i] = new;
      end
    endfunction
  endclass

  initial begin
    automatic txn t = new;
    automatic int bad = 0;
    repeat (20) begin
      if (!t.randomize()) bad++;
      else begin
        foreach (t.it[i]) begin
          if (t.it[i].addr[1:0] != 0) bad++;
          if (t.it[i].kind == 3 || (t.it[i].kind == 0 && t.it[i].len != 1))
            bad++;
          if (i > 0 && t.it[i].addr <= t.it[i-1].addr) bad++;
        end
      end
    end
    if (bad != 0) $error("bad=%0d", bad);
    else $display("PASSED");
  end
endmodule
