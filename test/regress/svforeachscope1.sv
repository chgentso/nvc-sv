// foreach over arrays named through a class scope, a parameterized class scope and a package scope
// Expected values: IEEE 1800-2017 12.7.3 (foreach over any array, indices in ascending order for [0:n-1]
// ranges), 8.10 (static members), 8.25 (parameterized classes, C#(int)::name), 26.3 (package scope), 7.10.1 (queue
// indices 0..size-1), 7.4.2 (int sa[4] has indices 0..3).
package pk; int g[3] = '{7, 8, 9}; string lg = ""; endpackage
class C #(type T = int);
  static T arr[$];
  static int sa[4];
  typedef C#(T) this_type;
  static function void add(T v); arr.push_back(v); endfunction
  function void show();
    foreach (this_type::arr[i]) pk::lg = {pk::lg, $sformatf("a %0d %0d;", i, this_type::arr[i])};
    foreach (C#(T)::sa[j]) pk::lg = {pk::lg, $sformatf("s %0d;", j)};
  endfunction
endclass
module svforeachscope1;
  int errs = 0;
  initial begin
    automatic C#(int) c = new;
    C#(int)::add(5); C#(int)::add(6);
    c.show();
    foreach (pk::g[k]) pk::lg = {pk::lg, $sformatf("g %0d %0d;", k, pk::g[k])};
    foreach (C#(int)::arr[m]) pk::lg = {pk::lg, $sformatf("m %0d;", m)};
    $display("%s", pk::lg);
    if (pk::lg != "a 0 5;a 1 6;s 0;s 1;s 2;s 3;g 0 7;g 1 8;g 2 9;m 0;m 1;") begin
      $error("log: got '%s'", pk::lg); errs++;
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
