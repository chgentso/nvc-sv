// Variable-index select of a VHDL array-of-record signal inside always @*:
// the block must be re-evaluated when the index changes and when any element
// of the array changes; change event on a record field.
// Expected values derived from the VHDL initial values and update schedule
// in mixed52.vhd (IEEE 1076 signal semantics) and IEEE 1800-2017 9.4.2.2
// (@* is sensitive to every variable read, including a variable index),
// 11.5.2 (array element select).  Times in ns; change events at time zero are
// not checked.
timeunit 1ns; timeprecision 100ps;
module mixed52;
  mixed52_e u();
  integer j;
  reg [7:0] r;
  reg v;
  integer errors = 0;
  string ev_num[$];
  always @* r = u.ra[j].vec;
  always @* v = u.ra[j].valid;
  task automatic chk(string what, string got, string exp);
    if (got !== exp) begin $error("%s: got '%s' expected '%s'", what, got, exp); errors++; end
  endtask
  always @(u.r.num) if ($realtime > 0) ev_num.push_back($sformatf("%0d %0d", $realtime, u.r.num));
  initial begin
    j = 2;
    #1 chk("a", $sformatf("%b %b", r, v), "00001111 1");
    j = 4;
    #1 chk("b", $sformatf("%b %b", r, v), "00001111 1");        // t=2: ra(4) not yet changed
    #5 chk("c", $sformatf("%b %b", r, v), "10000001 1");        // t=7: ra(4).vec = x"81" since 5
    #5 chk("d", $sformatf("%b %b", r, v), "10000001 0");        // t=12: ra(4).valid = '0' since 10
    j = 3;
    #1 chk("e", $sformatf("%b %b", r, v), "00001111 1");        // t=13: ra(3)
    if (ev_num.size() != 1 || ev_num[0] != "5 6") begin
      $error("num events: %0d, first '%s' expected one '5 6'", ev_num.size(),
             ev_num.size() > 0 ? ev_num[0] : "-");
      errors++;
    end
    if (errors == 0) $display("PASSED");
  end
endmodule
