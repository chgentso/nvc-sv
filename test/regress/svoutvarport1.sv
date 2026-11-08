// Variable output ports driven through a hierarchy by continuous assignments in leaf modules
// Expected values: IEEE 1800-2017 23.2.2.3 (output ports of variable kind, single continuous driver),
// 6.8 (an undriven 4-state variable stays x), 10.3
`timescale 1ns/1ns
module leaf (output logic [3:0] e);
  assign e = 4'd5;
endmodule
module leaf_idle (output logic [3:0] e);
endmodule
module mid1 (output logic [3:0] a_o, output logic [3:0] b_o);
  leaf a (.e(a_o));
  leaf b (.e(b_o));
endmodule
module mid2 (output logic [3:0] a_o);
  leaf_idle a (.e(a_o));
endmodule
module mid3 (output wire [3:0] a_o);
  leaf a (.e(a_o));
endmodule
module svoutvarport1;
  logic [3:0] x1, y1, x2, x3;
  logic [3:0] arr [3];
  mid1 m1 (.a_o(x1), .b_o(y1));
  mid2 m2 (.a_o(x2));
  mid3 m3 (.a_o(x3));
  mid1 m4 (.a_o(arr[1]), .b_o(arr[2]));
  initial begin
    #10;
    if (x1 !== 4'd5 || y1 !== 4'd5) $error("m1: x1=%b y1=%b", x1, y1);
    else if (x2 !== 4'bxxxx) $error("m2: x2=%b expected xxxx", x2);
    else if (x3 !== 4'd5) $error("m3: x3=%b", x3);
    else if (arr[1] !== 4'd5 || arr[2] !== 4'd5) $error("m4: arr=%b %b", arr[1], arr[2]);
    else $display("PASSED");
  end
endmodule
