// Packed struct locals in automatic/static tasks and functions: member writes in either order.
// Expected values: IEEE 1800-2017 7.2.1 (packed struct: first member is most significant),
// 13.3.1/13.4.2 (automatic locals), 21.2.1 (%b)
package p;
  typedef struct packed { logic [7:0] data; logic last; } beat_t;
  typedef struct packed { beat_t b; logic [3:0] id; } pkt_t;
endpackage

module svpstructauto1;
  import p::*;
  int errs = 0;
  function automatic void chk(string n, logic [12:0] got, logic [12:0] exp);
    if (got !== exp) begin errs++; $error("%s: got %b expected %b", n, got, exp); end
  endfunction
  localparam logic [8:0] BEAT = 9'b0100_0010_1;           // {8'h42, 1'b1}
  localparam logic [12:0] PKT = 13'b1010_0101_1_1001;     // {{8'hA5, 1'b1}, 4'h9}
  task automatic a1; beat_t bt; bt.data = 8'h42; bt.last = 1; chk("a1", bt, BEAT); endtask
  task automatic a2; beat_t bt; bt.last = 1; bt.data = 8'h42; chk("a2", bt, BEAT); endtask
  task automatic a3; beat_t bt = '0; bt.data = 8'h42; bt.last = 1; chk("a3", bt, BEAT); endtask
  task a4; beat_t bt; bt.data = 8'h42; bt.last = 1; chk("a4", bt, BEAT); endtask
  function automatic void a5; beat_t bt; bt.data = 8'h42; bt.last = 1; chk("a5", bt, BEAT); endfunction
  task automatic a6; pkt_t pk = '0; pk.id = 4'h9; pk.b.data = 8'hA5; pk.b.last = 1; chk("a6", pk, PKT); endtask
  initial begin
    a1; a2; a3; a4; a5; a6;
    if (errs == 0) $display("PASSED");
  end
endmodule
