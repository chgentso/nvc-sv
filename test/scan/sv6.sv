module `UNDEFINED_NAME
  (input a);
endmodule

module after_bad (input b);
  child u (.b(b));
endmodule
