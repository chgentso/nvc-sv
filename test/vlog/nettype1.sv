// IEEE 1800-2017 6.6.7: the data type of a nettype is a 4-state or 2-state
// integral type, real or shortreal, or a fixed-size unpacked array, struct or
// union of such types; the resolution function is an automatic function (or
// one keeping no state) with a single input argument that is a dynamic array
// of the nettype's data type, returning that type; a nettype without a
// resolution function allows a single driver.  10.3 / 10.4: a net is driven
// by continuous assignments and ports, never by procedural assignment.
module nettype1;
  class C; endclass
  typedef struct { real r; C h; } bad_s;
  typedef int dyn_t[];
  function automatic real ok_res (input real d[]); return 0.0; endfunction
  function automatic int bad_ret (input real d[]); return 0; endfunction
  function automatic real bad_arg (input real d); return d; endfunction
  function automatic real bad_two (input real d[], input int n); return 0.0; endfunction
  function real bad_static (input real d[]); return 0.0; endfunction
  function automatic real bad_dir (ref real d[]); return 0.0; endfunction
  task automatic bad_task (input real d[]); endtask

  nettype real ok_net with ok_res;      // OK
  nettype real n1 with bad_ret;         // Error
  nettype real n2 with bad_arg;         // Error
  nettype real n3 with bad_two;         // Error
  nettype real n4 with bad_static;      // OK (keeps no state)
  nettype real n5 with bad_dir;         // Error
  nettype real n6 with bad_task;        // Error
  nettype string n7;                    // Error
  nettype bad_s n8;                     // Error
  nettype dyn_t n9;                     // Error
  nettype real plain;                   // OK

  ok_net a;
  plain p, q;
  assign a = 1.0;                       // OK
  assign a = 2.0;                       // OK
  assign p = 1.0;                       // OK
  assign q = 1.0;
  assign q = 2.0;                       // Error
  initial a = 3.0;                      // Error
  always @(a) p <= a;                   // Error
endmodule
