class C;
  int id;
  function new(int i); id = i; endfunction
endclass

module svassochandle1;
  int a[C];
  C objs[$];
  C c;
  initial begin
    for (int i = 0; i < 6; i++) begin
      c = new(i);
      objs.push_back(c);
      a[c] = i * 10;
    end
    foreach (a[k]) $write("%0d ", k.id);
    $display("");
    if (a.first(c)) do $write("%0d ", c.id); while (a.next(c));
    $display("");
    a.delete(objs[3]);
    foreach (a[k]) $write("%0d ", k.id);
    $display("");
    c = new(9); a[c] = 1;
    foreach (a[k]) $write("%0d ", k.id);
    $display("");
  end
endmodule
