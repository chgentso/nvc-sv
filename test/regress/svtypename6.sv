// $typename corner cases.
// IEEE 1800-2017 20.6.1: $typename of an expression is the name of its type.
// - An anonymous enum, structure or union type is given a generated name
//   qualified by the scope that declares it; for a module this is the module
//   name (the declaration, not the instance path), so every instance of a
//   non-top module gives the same string, and two different anonymous types
//   give different strings.  The generated part after the scope is
//   implementation-defined and only checked to contain a "$".
// - A type declared in a class is qualified by the class followed by ::
//   (the class scope resolution operator, 8.23), whether the class is in a
//   package or in a module, as a class nested in a class is; a class-scoped
//   type may also be given as the argument (20.6.1: a data type argument).
// - A net has the data type of the net (6.7.1); an implicit net is a scalar
//   net of type logic (6.10).
// - An indexed part-select has a constant width and its result is an
//   unsigned packed array of that width with the remaining packed
//   dimensions (11.5.1, 7.4.3: selecting a slice of the first packed
//   dimension keeps the others), "[w-1:0]", whatever the signing of the
//   selected vector.
// - An element of an unpacked array has the element type with the
//   remaining unpacked dimensions (7.4.5), with a constant or a variable
//   index.
// Whitespace is ignored in the comparisons.
package svtypename6_pkg;
  class K;
    typedef enum {R2, S2} t;
    t v;
  endclass
endpackage

module svtypename6_sub;
  string se, ss, su, sn;
  enum {X, Y} e;
  struct packed { bit a; logic [2:0] b; } s;
  union packed { bit [3:0] w; logic [3:0] l; } u;
  typedef struct { int q; } named_t;
  named_t n;
  initial begin
    se = $typename(e);
    ss = $typename(s);
    su = $typename(u);
    sn = $typename(n);
  end
endmodule

module svtypename6;
  svtypename6_sub i1();
  svtypename6_sub i2();

  class C;
    enum {P, Q} m;
    typedef enum {R, S} t;
    t tv;
    typedef struct packed { bit a; } ps_t;
    ps_t pv;
    function string sm(); return $typename(m); endfunction
    function string stv(); return $typename(tv); endfunction
    function string spv(); return $typename(pv); endfunction
  endclass

  assign iw = 1'b1;
  wire w;
  wire [3:0] w4;

  bit [7:0] v;
  logic [7:0] lv;
  int iv;
  bit signed [7:0] sv;
  bit [3:0][7:0] pv;
  int i = 1;
  int a[4];
  int q[$] = '{1, 2};
  logic [3:0] m [2][3];
  byte d[] = new [2];

  int errs = 0;

  function automatic string nows(string s);
    string r = "";
    foreach (s[k]) if (s[k] != " ") r = {r, string'(s[k])};
    return r;
  endfunction

  function automatic void check(string what, string got, string exp);
    if (nows(got) != nows(exp)) begin
      $display("%s: got '%s' expected '%s'", what, got, exp);
      errs++;
    end
  endfunction

  // got is the expected scope-qualified prefix followed by a generated name
  function automatic void check_anon(string what, string got, string prefix);
    string g = nows(got), p = nows(prefix), rest;
    bit dollar = 0;
    if (g.len() <= p.len() || g.substr(0, p.len() - 1) != p) begin
      $display("%s: got '%s' expected '%s<name>'", what, got, prefix);
      errs++;
      return;
    end
    rest = g.substr(p.len(), g.len() - 1);
    foreach (rest[k]) if (rest[k] == "$") dollar = 1;
    if (!dollar) begin
      $display("%s: generated name '%s' has no $", what, rest);
      errs++;
    end
  endfunction

  initial begin
    automatic C c = new;
    automatic svtypename6_pkg::K k = new;
    #1;

    // Anonymous types in a module that is not the top
    check_anon("sub e", i1.se, "enum{X=32'sd0,Y=32'sd1}svtypename6_sub.");
    check_anon("sub s", i1.ss, "struct packed{bit a;logic[2:0] b;}svtypename6_sub.");
    check_anon("sub u", i1.su, "union packed{bit[3:0] w;logic[3:0] l;}svtypename6_sub.");
    check("sub n", i1.sn, "struct{int q;}svtypename6_sub.named_t");
    check("instances e", i2.se, i1.se);
    check("instances s", i2.ss, i1.ss);
    check("instances u", i2.su, i1.su);
    check("instances n", i2.sn, i1.sn);
    if (i1.ss == i1.su) begin $display("anonymous struct and union alike"); errs++; end

    // Class scope
    check_anon("class anon", c.sm(), "enum{P=32'sd0,Q=32'sd1}svtypename6.C::");
    check("class typedef", c.stv(), "enum{R=32'sd0,S=32'sd1}svtypename6.C::t");
    check("class typedef obj", $typename(c.tv), c.stv());
    check("class packed", c.spv(), "struct packed{bit a;}svtypename6.C::ps_t");
    check("class type arg", $typename(C::t), c.stv());
    check("class type arg 2", $typename(C::ps_t), c.spv());
    check("pkg class typedef", $typename(k.v),
          "enum{R2=32'sd0,S2=32'sd1}svtypename6_pkg::K::t");
    check("pkg class type arg", $typename(svtypename6_pkg::K::t), $typename(k.v));

    // Nets
    check("implicit net", $typename(iw), "logic");
    check("wire", $typename(w), "logic");
    check("wire vector", $typename(w4), "logic[3:0]");

    // Indexed part-selects
    check("v[i+:2]", $typename(v[i+:2]), "bit[1:0]");
    check("lv[i-:3]", $typename(lv[i-:3]), "logic[2:0]");
    check("iv[i+:2]", $typename(iv[i+:2]), "bit[1:0]");
    check("sv[i+:4]", $typename(sv[i+:4]), "bit[3:0]");
    check("pv[i+:2]", $typename(pv[i+:2]), "bit[1:0][7:0]");
    check("pv[1]", $typename(pv[1]), "bit[7:0]");

    // Elements of unpacked arrays
    check("a[1]", $typename(a[1]), "int");
    check("a[i]", $typename(a[i]), "int");
    check("q[0]", $typename(q[0]), "int");
    check("d[i]", $typename(d[i]), "byte");
    check("m[1]", $typename(m[1]), "logic[3:0]$[0:2]");
    check("m[i]", $typename(m[i]), "logic[3:0]$[0:2]");
    check("m[1][2]", $typename(m[1][2]), "logic[3:0]");
    check("m[i][i]", $typename(m[i][i]), "logic[3:0]");
    check("m[1][2][0]", $typename(m[1][2][0]), "logic");

    if (errs == 0) $display("PASSED");
    else $error("%0d mismatches", errs);
  end
endmodule
