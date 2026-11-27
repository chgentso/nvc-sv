class Box #(type T = int);
  T value;
  extern static function Box#(Box::T) make(T v);
endclass

function Box#(Box::T) Box::make(T v);
  Box#(T) obj = new();
  obj.value = v;
  return obj;
endfunction

module sv_extern_param_ret1;
  initial begin
    automatic Box#(string) bs = Box#(string)::make("hello");
    automatic Box#(int) bi = Box#(int)::make(42);
    if (bs.value !== "hello" || bi.value !== 42)
      $fatal(1, "got %s %0d", bs.value, bi.value);
    $display("PASSED");
  end
endmodule
