// Expected values: IEEE 1800-2017 9.4.2.2 (implicit event_expression @*: sensitive to the variables read
// in the statement, evaluated lazily, and no run at time zero unless an event occurs), 6.8 (a declaration
// initializer causes no event), 4.9.3/9.4.2 (NBA updates are visible one time step later after the
// block ran at the earlier step), 11.5.2 (struct member select of an array element).
// Whether a write to list[2] while ll_addr selects list[1] re-triggers the block (whole-array versus
// selected-element sensitivity) is not fixed by the LRM text; the test allows either and checks that
// the count stays consistent afterwards.
`timescale 1ns/1ns
module svstarsens1;
  typedef struct {
    int next;
    bit valid;
    bit eof;
  } e_t;
  e_t list [4];
  reg [3:0] ll_addr = 1;
  reg [7:0] packed_d;
  int cnt = 0;
  int errs = 0;
  int extra;

  always @(*) begin
    packed_d[0] <= list[ll_addr].valid;
    if (list[ll_addr].eof)
      packed_d[3:1] <= 3'd7;
    else
      packed_d[7:4] <= list[ll_addr].next;
    cnt++;
  end

  task automatic set(int i, int n, bit v, bit e);
    list[i].next = n;
    list[i].valid = v;
    list[i].eof = e;
  endtask

  task automatic chk(string w, logic [7:0] exp, int expcnt);
    if (packed_d !== exp) begin $error("%s: packed=%b expected %b", w, packed_d, exp); errs++; end
    if (cnt != expcnt) begin $error("%s: cnt=%0d expected %0d", w, cnt, expcnt); errs++; end
  endtask

  initial begin
    #1 chk("t1", 8'bxxxxxxxx, 0);
    set(1, 5, 1, 0);
    #1 chk("t2", 8'b0101xxx1, 1);
    list[1].next = 9;
    #1 chk("t3", 8'b1001xxx1, 2);
    set(2, 3, 1, 1);
    #1 // other index: block may or may not run (extra 0 or 1); values are unchanged
    extra = cnt - 2;
    if (extra != 0 && extra != 1) begin $error("other index: cnt=%0d", cnt); errs++; end
    chk("t4", 8'b1001xxx1, 2 + extra);
    ll_addr = 2;
    #1 chk("t5", 8'b10011111, 3 + extra);
    list[2].eof = 0;
    #1 chk("t6", 8'b00111111, 4 + extra);
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
