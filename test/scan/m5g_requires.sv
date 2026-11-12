package m5g_req;
  import uvm_pkg::*;

  class base extends uvm_object;
    typedef uvm_object_registry #(base, "base") type_id;
    static function type_id get_type();
      return type_id::get();
    endfunction
  endclass

  typedef uvm_sequencer #(base) seqr_t;
  typedef enum {A, B} e_t;

  class user;
    function void f();
      seqr_t s;
      base b;
      e_t e = e_t::A;
      s = seqr_t::type_id::create("s");
      b = base::type_id::create("b");
      b = user_pkg::inner::thing::create("x");
      uvm_report_cb::add(null, null);
    endfunction
  endclass
endpackage
