`timescale 1ns/1ns
// The nmos and pmos switches and an unknown enable of a gate
// Expected values: IEEE 1800-2017 28.4 (MOS switches: Table 28-6, an off switch gives z, an on switch passes the
// data input; with an unknown or high-impedance control the result is the ambiguous L (0 or z) or H (1 or z) for a
// known data value), 28.5 (bufif/notif: Table 28-5).
// L and H have no 4-state encoding of their own: a net that sees an L shows 0, z or x (strength ambiguity), so the
// test accepts any of those for L (0 or z or x) and for H (1 or z or x), never the opposite value.
module svmosgate1;
  reg i, c;
  wire o1, o2, o3, o4, o5, o6;
  nmos n1(o1, i, c);
  pmos p1(o2, i, c);
  rnmos n2(o3, i, c);
  rpmos p2(o4, i, c);
  bufif1 b1(o5, i, c);
  notif0 b2(o6, i, c);
  int fails = 0;
  // exact expected values: 0, 1, z; 'L': 0/z/x; 'H': 1/z/x
  task automatic chk(input string tag, input logic [5:0] o, input string e);
    for (int k = 0; k < 6; k++) begin
      logic v = o[5-k];
      string ek = e.substr(k, k);
      bit ok;
      case (ek)
        "0": ok = (v === 1'b0);
        "1": ok = (v === 1'b1);
        "z": ok = (v === 1'bz);
        "L": ok = (v === 1'b0 || v === 1'bz || v === 1'bx);
        "H": ok = (v === 1'b1 || v === 1'bz || v === 1'bx);
        default: ok = 0;
      endcase
      if (!ok) begin $error("%s: output %0d is %b, expected %s", tag, k+1, v, ek); fails++; end
    end
  endtask
  initial begin
    i = 1; c = 0; #1 chk("i=1 c=0", {o1, o2, o3, o4, o5, o6}, "z1z1z0");
    c = 1; #1 chk("i=1 c=1", {o1, o2, o3, o4, o5, o6}, "1z1z1z");
    i = 0; #1 chk("i=0 c=1", {o1, o2, o3, o4, o5, o6}, "0z0z0z");
    c = 1'bx; #1 chk("i=0 c=x", {o1, o2, o3, o4, o5, o6}, "LLLLLH");
    c = 1'bz; i = 1; #1 chk("i=1 c=z", {o1, o2, o3, o4, o5, o6}, "HHHHHL");
    if (fails == 0) $display("PASSED");
  end
endmodule
