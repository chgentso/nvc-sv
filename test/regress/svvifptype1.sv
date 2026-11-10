// Self-checking. Expected values: IEEE 1800-2017 25.9 (a virtual interface
// type can give values to the parameters of the interface, including a type
// parameter, so the members of a port of that type are those of the type given
// and not those of the default), 6.22.1 (matching types), 8.25 (a class
// specialisation argument may be such a virtual interface type), 26.3 (a name
// qualified with a package scope names a type declared there) and 7.2.1
// (members of a packed structure).
package svvifptype1_pkg;
  typedef struct packed { logic valid; logic [1:0][7:0] key; } req_t;
  typedef struct packed { logic valid; logic [1:0][15:0] key; } wreq_t;
endpackage

interface svvifptype1_if #(parameter type T = svvifptype1_pkg::req_t)
  (input logic clk, output T k);
endinterface

class svvifptype1_hold #(type T = int);
  static T val;
endclass

module svvifptype1;
  svvifptype1_pkg::wreq_t w;
  svvifptype1_if #(svvifptype1_pkg::wreq_t) i(.clk(1'b0), .k(w));
  virtual svvifptype1_if #(svvifptype1_pkg::wreq_t) v;
  int errs = 0;
  initial begin
    svvifptype1_hold #(virtual svvifptype1_if #(svvifptype1_pkg::wreq_t))::val = i;
    v = svvifptype1_hold #(virtual svvifptype1_if #(svvifptype1_pkg::wreq_t))::val;
    if (v == null) begin errs++; $error("handle lost"); end
    v.k.valid = 1;
    v.k.key[0] = 16'h1234;
    v.k.key[1] = 16'habcd;
    #1;
    if (w.valid !== 1'b1 || w.key[0] !== 16'h1234 || w.key[1] !== 16'habcd) begin
      errs++; $error("w = %b %h %h", w.valid, w.key[0], w.key[1]);
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
