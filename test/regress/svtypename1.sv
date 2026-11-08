// Expected values: IEEE 1800-2017 20.6.1 ($typename with a data type argument),
// 6.18 (typedef), 6.11 (integer data types, default signing).
// Checked: the plain keyword types print as their keyword (with "unsigned"
// appended for types that are signed by default); a typedef of a built-in
// type resolves to the underlying type, so it prints like that type; an
// object and the type of that object print alike; an unpacked array prints
// its element type and its range.  The spacing inside vector ranges and the
// separator before an unpacked dimension are not compared literally.
// (A logic vector is expected to print with the keyword "logic".)

typedef int myint;
typedef bit [7:0] byte_t;
typedef logic signed [3:0] sl_t;
typedef int unsigned uint_t;
typedef enum {X, Y} en_t;
typedef struct packed { logic a; logic [1:0] b; } ps_t;
typedef struct { int a; } us_t;
module svtypename1;
  logic [5:0] v;
  int q [4];
  string s;
  bit [7:0] bv8;
  bit signed [7:0] bs8;
  int unsigned iu;
  int errs;
  function automatic bit has(string str, string sub);
    for (int i = 0; i + sub.len() <= str.len(); i++) if (str.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  task automatic eq(string got, string exp, string what);
    if (got != exp) begin $error("%s: got \"%s\", expected \"%s\"", what, got, exp); errs++; end
  endtask
  initial begin
    string tq, tv;
    eq($typename(int), "int", "int");
    eq($typename(integer), "integer", "integer");
    eq($typename(byte), "byte", "byte");
    eq($typename(bit), "bit", "bit");
    eq($typename(real), "real", "real");
    eq($typename(string), "string", "string");
    eq($typename(longint unsigned), "longint unsigned", "longint unsigned");
    eq($typename(myint), $typename(int), "typedef of int");
    eq($typename(uint_t), "int unsigned", "typedef of int unsigned");
    eq($typename(uint_t), $typename(iu), "typedef vs object");
    eq($typename(byte_t), $typename(bv8), "typedef bit [7:0] vs object");
    eq($typename(bit signed [7:0]), $typename(bs8), "bit signed [7:0] vs object");
    eq($typename(s), "string", "string object");
    eq($typename(sl_t), $typename(logic signed [3:0]), "typedef logic signed [3:0]");
    tq = $typename(bv8); tv = $typename(bs8);
    if (tq == tv) begin $error("signed and unsigned vectors print alike"); errs++; end
    tv = $typename(v);
    // logic and reg are synonyms; the declared keyword is "logic" (20.6.1 prints logic)
    if (!has(tv, "logic") || !has(tv, "5:0")) begin $error("logic [5:0] object printed as \"%s\"", tv); errs++; end
    tq = $typename(q);
    if (!has(tq, "int") || !has(tq, "[0:3]")) begin $error("int q[4] printed as \"%s\"", tq); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
