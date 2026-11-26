// Test small pure-combinational SCCs in SV RTL islands:
//  1) 2-proc false combinational cycle (controller-style is_decoding / jr_stall)
//  2) 3-proc false combinational cycle (id_stage-style imm_b / operand_b / shuffle)
//  3) True converging combinational feedback loop (converges in 3 iterations)
//  4) Disjoint bit-slice self-assign (mult-style short_op[16] = sign & short_op[15])

module scc_2proc (
  input  logic [1:0] mode_i,
  input  logic       jalr_i,
  input  logic       dep_i,
  output logic       is_decoding_o,
  output logic       jr_stall_o,
  output logic       deassert_we_o,
  output logic       halt_if_o
);
  // Proc A reads jr_stall_o and writes is_decoding_o, halt_if_o
  always_comb begin
    is_decoding_o = (mode_i == 2'b01);
    halt_if_o     = 1'b0;
    if (mode_i == 2'b01 && jr_stall_o)
      halt_if_o = 1'b1;
  end

  // Proc B reads is_decoding_o and writes jr_stall_o, deassert_we_o
  always_comb begin
    jr_stall_o    = jalr_i & dep_i;
    deassert_we_o = ~is_decoding_o | jr_stall_o;
  end
endmodule

module scc_3proc (
  input  logic       sel_imm_i,
  input  logic       vec_mode_i,
  input  logic [7:0] imm_a_i,
  input  logic [7:0] imm_b_i,
  input  logic [7:0] reg_i,
  output logic [7:0] op_b_o,
  output logic [7:0] op_b_vec_o
);
  logic [7:0] imm_mux;
  logic [7:0] op_b;
  logic [7:0] imm_shuf;

  // Proc 1: reads imm_shuf -> writes imm_mux
  always_comb begin
    imm_mux = sel_imm_i ? imm_shuf : imm_a_i;
  end

  // Proc 2: reads imm_mux -> writes op_b
  always_comb begin
    op_b = (reg_i == 8'h00) ? imm_mux : (reg_i ^ imm_mux);
  end

  // Proc 3: reads op_b -> writes op_b_vec_o and imm_shuf (false cycle via imm_shuf)
  always_comb begin
    op_b_vec_o = vec_mode_i ? {op_b[3:0], op_b[3:0]} : op_b;
    imm_shuf   = vec_mode_i ? imm_b_i : ~imm_b_i;
  end

  assign op_b_o = op_b;
endmodule

module scc_converge (
  input  logic [3:0] seed_i,
  output logic [3:0] a_o,
  output logic [3:0] b_o,
  output logic [3:0] c_o
);
  logic [3:0] a, b, c;

  // True converging 3-proc loop: carries propagate one bit per iteration
  always_comb begin
    a = seed_i | {c[2:0], 1'b0};
  end

  always_comb begin
    b = a | {1'b0, a[3:1]};
  end

  always_comb begin
    c = {b[3], b[2] & seed_i[1], b[1], seed_i[0]};
  end

  assign a_o = a;
  assign b_o = b;
  assign c_o = c;
endmodule

module scc_slice_self (
  input  logic       is_signed_i,
  input  logic [7:0] x_i,
  output logic [8:0] ext_o
);
  logic [8:0] tmp;
  assign tmp[7:0] = x_i;
  assign tmp[8]   = is_signed_i & tmp[7];
  assign ext_o    = tmp;
endmodule

module vlog_island_scc1;
  logic [1:0] mode_i = 2'b00;
  logic       jalr_i = 1'b0, dep_i = 1'b0;
  logic       is_decoding_o, jr_stall_o, deassert_we_o, halt_if_o;

  scc_2proc u2 (.*);

  logic       sel_imm_i = 1'b0, vec_mode_i = 1'b0;
  logic [7:0] imm_a_i = 8'hA5, imm_b_i = 8'h3C, reg_i = 8'h00;
  logic [7:0] op_b_o, op_b_vec_o;

  scc_3proc u3 (.*);

  logic [3:0] seed_i = 4'b0000;
  logic [3:0] a_o, b_o, c_o;

  scc_converge uc (.*);

  logic       is_signed_i = 1'b0;
  logic [7:0] x_i = 8'h81;
  logic [8:0] ext_o;

  scc_slice_self us (.*);

  initial begin
    #1;
    // Check 2-proc false cycle
    if (is_decoding_o !== 1'b0 || jr_stall_o !== 1'b0 || deassert_we_o !== 1'b1 || halt_if_o !== 1'b0)
      $fatal(1, "u2 init mismatch: %b %b %b %b", is_decoding_o, jr_stall_o, deassert_we_o, halt_if_o);

    mode_i = 2'b01; jalr_i = 1'b1; dep_i = 1'b1;
    #1;
    if (is_decoding_o !== 1'b1 || jr_stall_o !== 1'b1 || deassert_we_o !== 1'b1 || halt_if_o !== 1'b1)
      $fatal(1, "u2 stall mismatch: %b %b %b %b", is_decoding_o, jr_stall_o, deassert_we_o, halt_if_o);

    dep_i = 1'b0;
    #1;
    if (is_decoding_o !== 1'b1 || jr_stall_o !== 1'b0 || deassert_we_o !== 1'b0 || halt_if_o !== 1'b0)
      $fatal(1, "u2 nostall mismatch: %b %b %b %b", is_decoding_o, jr_stall_o, deassert_we_o, halt_if_o);

    // Check 3-proc false cycle
    sel_imm_i = 1'b1; vec_mode_i = 1'b0; reg_i = 8'h00;
    #1;
    if (op_b_o !== 8'hC3 || op_b_vec_o !== 8'hC3)
      $fatal(1, "u3 mode0 mismatch: %h %h", op_b_o, op_b_vec_o);

    vec_mode_i = 1'b1;
    #1;
    if (op_b_o !== 8'h3C || op_b_vec_o !== 8'hCC)
      $fatal(1, "u3 mode1 mismatch: %h %h", op_b_o, op_b_vec_o);

    reg_i = 8'h0F;
    #1;
    if (op_b_o !== 8'h33 || op_b_vec_o !== 8'h33)
      $fatal(1, "u3 xor mismatch: %h %h", op_b_o, op_b_vec_o);

    // Check true converging 3-proc loop
    seed_i = 4'b0011;
    #1;
    // Fixed point for seed=0011:
    //  iter1: a=0011, b=0011, c=0011
    //  iter2: a=0111, b=0111, c=0111
    //  iter3: a=1111, b=1111, c=1111
    if (a_o !== 4'b1111 || b_o !== 4'b1111 || c_o !== 4'b1111)
      $fatal(1, "uc conv1 mismatch: a=%b b=%b c=%b", a_o, b_o, c_o);

    seed_i = 4'b0001;
    #1;
    // Fixed point for seed=0001: c[2] forced 0 since seed[1]=0 -> a=0011, b=0011, c=0011
    // Wait: a = 0001 | {c[2:0], 0}: c=0011 -> {c[2:0],0}=0110 -> a=0111 -> b=0111 -> c={0,0,1,1}=0011!
    if (a_o !== 4'b0111 || b_o !== 4'b0111 || c_o !== 4'b0011)
      $fatal(1, "uc conv2 mismatch: a=%b b=%b c=%b", a_o, b_o, c_o);

    // Check disjoint bit-slice self-assign
    is_signed_i = 1'b0; x_i = 8'h81;
    #1;
    if (ext_o !== 9'h081)
      $fatal(1, "us unsigned mismatch: %h", ext_o);

    is_signed_i = 1'b1;
    #1;
    if (ext_o !== 9'h181)
      $fatal(1, "us signed mismatch: %h", ext_o);

    $display("PASSED");
    $finish;
  end
endmodule
