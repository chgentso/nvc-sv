module svclassnew2;
  class base;
    int s = 2;
    function new(int def = 3);
      s = def;
    endfunction
  endclass

  class derived extends base(5);
    int a;
    function new(int def = 42);
      a = def;
    endfunction
  endclass

  class typed #(int t = 12) extends base;
    int a;
    function new(int def = 42);
      super.new(def + 3);
      a = def - t;
    endfunction
  endclass

  class params #(parameter p = 12);
    typedef enum { A = 10, B = 20 } e_type;
    localparam q = p + 1;
    int x;
    string str;
  endclass

  derived d;
  base b;
  params #(34) pr;
  params #(34) pr2;

  initial begin
    d = new(37);
    if (d.a !== 37 || d.s !== 5) $fatal(1, "d %0d %0d", d.a, d.s);

    b = typed#(.t(23))::new(.def(41));
    if (b.s !== 44) $fatal(1, "b.s = %0d", b.s);

    pr = new;
    pr.x = 7;
    pr.str = "hello";
    if (pr.p !== 34 || pr.q !== 35 || pr.B !== 20) $fatal(1, "params");

    pr2 = new pr;
    pr.x = 8;
    if (pr2.x !== 7 || pr2.str != "hello") $fatal(1, "copy %0d %s", pr2.x, pr2.str);
    $display("PASSED");
  end
endmodule
