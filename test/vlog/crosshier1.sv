// IEEE 1800-2017 19.6, A.2.11: cross_item ::= cover_point_identifier
// | variable_identifier, so a hierarchical name is not a cross item
interface ifc;
  logic a;
endinterface

module crosshier1;
  ifc i();
  bit v;
  covergroup cg;
    cp: coverpoint v;
    x: cross cp, i.a;                   // Error
    y: cross cp, v;                     // OK
    cq: coverpoint i.a;
    z: cross cp, cq;                    // OK
  endgroup
endmodule
