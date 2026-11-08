// Methods of a class declared in a module, specialised through a parameterised registry
// Expected values: IEEE 1800-2017 8.16 ($cast of a handle to its own class always
// succeeds), 8.7 (new), 8.25 (parameterised classes), 6.16 (string members), 8.20 (virtual methods)
class bseq;
  string name = "seq";
  virtual function void setup(); endfunction
endclass

class registry #(type T = int);
  static function T make();
    T t = new();
    return t;
  endfunction
endclass

module svclspec2;
  int errs = 0;
  int cast_failed = 0;
  class myseq extends bseq;
    typedef registry#(myseq) type_id;
    bseq other;
    virtual function void setup();
      myseq m;
      if (!$cast(m, other))
        cast_failed++;
    endfunction
  endclass

  initial begin
    automatic myseq s = myseq::type_id::make();
    s.other = s;
    if (s.name != "seq") begin $error("name is '%s', expected 'seq'", s.name); errs++; end
    s.setup();
    if (cast_failed != 0) begin $error("downcast of a myseq handle failed"); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
