// IEEE 1800-2017 3.13(a): a second module with the same name is an error;
// with --relaxed it is a warning and the last definition is used, as a
// design unit analysed later replaces one of the same name in a library.
module svdupmod2_sub; initial $display("not used"); endmodule
module svdupmod2_sub; initial $display("PASSED"); endmodule
module svdupmod2; svdupmod2_sub u(); endmodule
