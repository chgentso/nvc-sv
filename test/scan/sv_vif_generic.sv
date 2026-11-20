// The interface of a virtual interface type in a parameterised class is
// required although the body is only parsed for a specialisation
package vif_pkg;
  class agent #(parameter int W = 8);
    virtual vif_if#(W) vif;
    virtual interface other_if o;
  endclass
endpackage
