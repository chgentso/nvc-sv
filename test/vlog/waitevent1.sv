// IEEE 1800-2017 15.5.3: the condition of a wait statement cannot be a named
// event, which has no value, but its triggered property can.
module waitevent1;
  event e;
  logic ready;
  initial begin
    wait (e);                                   // Error
    wait (e.triggered);                         // OK
    wait (ready);                               // OK
  end
endmodule
