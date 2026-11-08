// $printtimescale of a name which is not a module instance
// IEEE 1800-2017 20.4.2: the argument is the hierarchical name of a module instance.
module svprinttimescale3;
  initial $printtimescale(nosuch.inst);
endmodule
