// A continuous assignment and an always_comb process are sensitive to the
// index of a select that comes before a member: the value follows a change
// of the index as well as a change of the element (IEEE 1800-2017 10.3.2
// and 9.2.2.2.1).
module svassignmemberidx1;
  typedef struct packed { logic valid; logic [3:0] key; } slot_t;
  slot_t [3:0] slots;
  logic [1:0] sel = 0;
  logic v_assign, v_comb;
  logic [3:0] k_assign, k_comb;
  int errs = 0;

  assign v_assign = slots[sel].valid;
  assign k_assign = slots[sel].key;
  always_comb v_comb = slots[sel].valid;
  always_comb k_comb = slots[sel].key;

  initial begin
    slots = {5'b1_0001, 5'b0_0010, 5'b1_0011, 5'b0_0100};
    for (int i = 0; i < 4; i++) begin
      #1 sel = i[1:0];
      #1;
      if (v_assign !== slots[sel].valid || k_assign !== slots[sel].key) begin
        errs++;
        $error("assign sel=%0d: valid=%b key=%b", sel, v_assign, k_assign);
      end
      if (v_comb !== slots[sel].valid || k_comb !== slots[sel].key) begin
        errs++;
        $error("comb sel=%0d: valid=%b key=%b", sel, v_comb, k_comb);
      end
    end
    #1 slots[2] = 5'b1_1111;
    #1 sel = 2;
    #1 slots[2] = 5'b0_1010;
    #1;
    if (v_assign !== 1'b0 || k_assign !== 4'ha || v_comb !== 1'b0 || k_comb !== 4'ha) begin
      errs++;
      $error("element change: %b %h %b %h", v_assign, k_assign, v_comb, k_comb);
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
