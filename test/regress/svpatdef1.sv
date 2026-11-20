module svpatdef1;
  typedef struct {
    int i;
    logic [7:0] b;
  } ustruct_t;

  typedef struct {
    int x;
    logic [3:0] y;
  } inner_t;

  typedef struct {
    inner_t in;
    int sub[2];
  } nested_t;

  int row[4];
  int mat[2][4];
  ustruct_t us1[3];
  ustruct_t us2[2][3];
  ustruct_t uval;
  ustruct_t usvar[3];
  ustruct_t usnest[3];
  nested_t ns;
  nested_t nsarr[2];

  initial begin
    row = '{10, 20, 30, 40};
    mat = '{default: row};
    foreach (mat[i, j])
      if (mat[i][j] !== row[j]) $fatal(1, "mat");

    us1 = '{default: '0};
    foreach (us1[i])
      if (us1[i].i !== 0 || us1[i].b !== 8'h00) $fatal(1, "us1");

    us2 = '{default: '1};
    foreach (us2[i, j])
      if (us2[i][j].i !== -1 || us2[i][j].b !== 8'hff) $fatal(1, "us2");

    uval = '{i: 7, b: 8'h5a};
    usvar = '{default: uval};
    uval.i = 99;
    foreach (usvar[i])
      if (usvar[i].i !== 7 || usvar[i].b !== 8'h5a) $fatal(1, "usvar");

    usnest = '{default: '{default: '1}};
    foreach (usnest[i])
      if (usnest[i].i !== -1 || usnest[i].b !== 8'hff) $fatal(1, "usnest");

    ns = '{default: '1};
    if (ns.in.x !== -1 || ns.in.y !== 4'hf || ns.sub[0] !== -1 || ns.sub[1] !== -1)
      $fatal(1, "ns");

    nsarr = '{default: '1};
    foreach (nsarr[i])
      if (nsarr[i].in.x !== -1 || nsarr[i].in.y !== 4'hf
          || nsarr[i].sub[0] !== -1 || nsarr[i].sub[1] !== -1)
        $fatal(1, "nsarr");

    $display("PASSED");
    $finish;
  end
endmodule
