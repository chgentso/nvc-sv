// Expected values: IEEE 1800-2017 7.2.1 (packed struct: data = bits [8:1], last = bit [0]), 7.4 (unpacked array of
// packed structs, select by variable index and by constant index), 25.3/25.5 (interface port), 23.6 (hierarchical
// reference), 10.4.1 (blocking assignment to a struct member of an array element).
`timescale 1ns/1ns
package p;
  typedef struct packed { logic [7:0] data; logic last; } beat_t;
endpackage

interface bi;
  import p::*;
  beat_t beat [4];
endinterface

module sub(bi ifc, input logic [1:0] cur);
  logic got_last;
  logic [7:0] got_data;
  logic [8:0] got_whole;
  initial begin
    #2;
    got_last = ifc.beat[cur].last; got_data = ifc.beat[cur].data;
    ifc.beat[cur].data = 8'h5a;
    #1;
    got_whole = ifc.beat[cur];
  end
endmodule

module svifcarrmember1;
  import p::*;
  bi b();
  logic [1:0] cur = 2;
  sub s(b, cur);
  int err = 0;
  `define CHK(got, exp) if ((got) !== (exp)) begin err++; $error("%s: got %b expected %b", `"got`", got, exp); end
  initial begin
    b.beat[0] = 9'h1ff; b.beat[1] = 9'h000; b.beat[2] = 9'h001; b.beat[3] = 9'h000;
    #1;
    `CHK(b.beat[cur].last, 1'b1) `CHK(b.beat[cur].data, 8'h00)
    `CHK(b.beat[2].last, 1'b1) `CHK(b.beat[2].data, 8'h00)
    `CHK(b.beat[3].last, 1'b0) `CHK(b.beat[3].data, 8'h00)
    b.beat[1].last = 1;
    #3;
    `CHK(s.got_last, 1'b1) `CHK(s.got_data, 8'h00) `CHK(s.got_whole, 9'h0b5)
    `CHK(b.beat[0], 9'h1ff) `CHK(b.beat[1], 9'h001) `CHK(b.beat[2], 9'h0b5) `CHK(b.beat[3], 9'h000)
    if (err == 0) $display("PASSED");
  end
endmodule
