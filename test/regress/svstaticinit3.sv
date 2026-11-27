// The initial value of a variable calls a static function declared
// later in the module: the result of the function is kept in the
// instance and must exist before the initialisation
module svstaticinit3;
  static int v = get(3);
  int w = twice(v);

  function static int get(int n);
    int acc;
    acc += n;
    return acc * 10;
  endfunction

  function int twice(int n);
    return 2 * n;
  endfunction

  initial begin
    $display("%0d %0d %0d", v, w, get(1));
  end
endmodule
