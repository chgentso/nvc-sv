`define MAGIC 4'h9

package ng_pkg;
    localparam int PKG_W = 12;
endpackage

module ng_sv_top;
    localparam LP = 6;
    localparam [3:0] NIB = 4'b0110;
    wire o1, o2, o3, o4;
    ng_sv_leaf #(.W(LP + 1), .NAME("sv"), .V(NIB)) u_lp (.o(o1));
    ng_sv_leaf #(.W(ng_pkg::PKG_W), .R(2.5)) u_pkg (.o(o2));
    ng_sv_leaf #(.W($clog2(33)), .V(`MAGIC), .B(1)) u_clog (.o(o3));
    ng_sv_leaf u_def (.o(o4));
endmodule
