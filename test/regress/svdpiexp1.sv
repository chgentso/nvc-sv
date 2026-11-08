// DPI-C exported functions/tasks called back from C in different scopes.
// All expected values below are computed by hand from the SV declarations and
// from the C helper (test/dpi/dpi_test.c) semantics:
//  - a context import calls an export in the scope of the calling instance,
//    svSetScope() redirects it to another instance (IEEE 1800-2017 35.5.3,
//    35.9, scope functions svGetScopeFromName / svSetScope / svGetNameFromScope),
//  - exported task/function formal passing (35.5.5, 35.6),
//  - svGetUserData/svPutUserData data is per scope (H.10 / 35).
// The text printed by the C helpers for dpi_call_lvec and
// dpi_call_str is not captured; the same SV functions are checked directly.
package expkg;
  int calls;
  export "DPI-C" function pkg_twice;
  function int pkg_twice(int a);
    calls++;
    return 2 * a;
  endfunction
  import "DPI-C" context function int dpi_call_pkg(int a);
  function int run(int a);
    return dpi_call_pkg(a);
  endfunction
endpackage

module leaf #(parameter int ID = 0);
  int state = ID * 100;
  int bumped = -1;          // result of the delayed bump
  string where = "";        // scope name seen by the C code
  string myname = "";       // %m of the instance
  int errors = 0;

  export "DPI-C" function sv_bump;
  export "DPI-C" function sv_store;
  export "DPI-C" task sv_task;

  function int sv_bump(int delta);
    state += delta;
    return state;
  endfunction

  function void sv_store(int value, output int old);
    old = state;
    state = value;
  endfunction

  task sv_task(input int a, input int b, output int result);
    result = a * b + state;
  endtask

  import "DPI-C" context function int dpi_call_bump(int delta);
  import "DPI-C" context task dpi_mix(input int a, input int b, output int r);
  import "DPI-C" context function string dpi_where();

  initial begin
    myname = $sformatf("%m");
    #(ID + 1);
    where = dpi_where();
    bumped = dpi_call_bump(1);
  end

  import "DPI-C" context function int dpi_userdata(int value);

  function int user(int v);
    return dpi_userdata(v);
  endfunction

  task automatic mix(int a, int b, int exp_r, int exp_state);
    int r;
    dpi_mix(a, b, r);
    if (r != exp_r || state != exp_state) begin
      $error("%m: mix result %0d state %0d, expected %0d / %0d", r, state, exp_r, exp_state);
      errors++;
    end
  endtask
endmodule

module svdpiexp1;
  import "DPI-C" context function int dpi_call_in(string path, int delta);
  import "DPI-C" context function longint dpi_call_wide();
  import "DPI-C" context function int dpi_call_logic();
  import "DPI-C" context function void dpi_call_lvec();
  import "DPI-C" context function void dpi_call_str();
  import "DPI-C" context function int dpi_call_alias(int a);
  import "DPI-C" context function int dpi_userdata(int value);

  leaf #(1) a();
  leaf #(2) b();
  leaf #(3) c();

  generate
    for (genvar i = 0; i < 2; i++) begin : g
      leaf #(10 + i) l();
    end
  endgenerate

  export "DPI-C" sv_alias_c = function aliased;
  export "DPI-C" function sv_wide;

  function int aliased(int a);
    return a * 3;
  endfunction
  export "DPI-C" function sv_lnot;
  export "DPI-C" function sv_lvec;
  export "DPI-C" function sv_greet;

  function longint sv_wide(longint a, byte unsigned b);
    return a + b;
  endfunction

  function logic sv_lnot(logic a);
    return ~a;
  endfunction

  function void sv_lvec(input logic [39:0] i, output logic [39:0] o);
    o = {i[19:0], i[39:20]};
  endfunction

  function string sv_greet(string name, output string upper);
    upper = name.toupper();
    return {"hello ", name};
  endfunction

  int errors = 0;
  task automatic chk(string what, longint got, longint exp);
    if (got != exp) begin $error("%s: got %0d expected %0d", what, got, exp); errors++; end
  endtask
  task automatic chks(string what, string got, string exp);
    if (got != exp) begin $error("%s: got '%s' expected '%s'", what, got, exp); errors++; end
  endtask

  int u1, u2, u3, u4, pk;
  logic [39:0] lo;
  string up, gr;
  initial begin
    #10;
    // a, b, c have already been bumped (at 2, 3, 4); g[*].l not yet (11, 12)
    chk("in a", dpi_call_in("svdpiexp1.a", 5), 101 + 5);
    chk("in c", dpi_call_in("svdpiexp1.c", 7), 301 + 7);
    chk("in g[1]", dpi_call_in("svdpiexp1.g[1].l", 9), 1100 + 9);
    chk("in none", dpi_call_in("svdpiexp1.nothing", 9), -1);
    chk("wide", dpi_call_wide(), 64'h1_0000_0007);
    // svLogic: sv_0=0 sv_1=1 sv_z=2 sv_x=3; ~0=1, ~x=x(3), ~1=0
    chk("logic", dpi_call_logic(), 32'h31);
    dpi_call_lvec();
    dpi_call_str();
    chk("alias", dpi_call_alias(5), 5 * 3 + 1);
    // user data is kept per scope: first use of a scope returns -1
    u1 = dpi_userdata(11);
    u2 = dpi_userdata(22);
    u3 = a.user(33);
    u4 = a.user(44);
    chk("userdata top first", u1, -1);
    chk("userdata top second", u2, 11);
    chk("userdata a first", u3, -1);
    chk("userdata a second", u4, 33);
    // a.state = 106 now
    a.mix(3, 4, 106 * 1000 + (3 * 4 + 3), 3);
    b.mix(5, 6, 201 * 1000 + (5 * 6 + 5), 5);
    pk = expkg::run(21);
    chk("pkg", pk, 42);
    chk("pkg calls", expkg::calls, 1);
    // directly called exports: the same SV code as reached from C
    sv_lvec(40'h01_0000_0055, lo);
    chk("sv_lvec", lo, 40'h00_0550_1000);
    gr = sv_greet("world", up);
    chks("greet", gr, "hello world");
    chks("greet upper", up, "WORLD");
    #10;
    chk("a bump", a.bumped, 101);
    chk("b bump", b.bumped, 201);
    chk("c bump", c.bumped, 301);
    chk("g0 bump", g[0].l.bumped, 1001);
    chk("g1 bump", g[1].l.bumped, 1100 + 9 + 1);
    chks("a where", a.where, a.myname);
    chks("b where", b.where, b.myname);
    chks("c where", c.where, c.myname);
    chks("g0 where", g[0].l.where, g[0].l.myname);
    chks("g1 where", g[1].l.where, g[1].l.myname);
    chks("a name", a.myname, "svdpiexp1.a");
    chks("g1 name", g[1].l.myname, "svdpiexp1.g[1].l");
    chk("leaf errors", a.errors + b.errors + c.errors, 0);
    if (errors == 0) $display("PASSED");
  end
endmodule
