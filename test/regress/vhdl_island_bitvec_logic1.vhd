-- Logical operators on bit_vector inside a fused combinational process
-- (byte-per-bit storage, Mode B): and, or, xor, nand, nor, xnor and not
entity bitvec_dut is
    port (
        clk : in  bit;
        a   : in  bit_vector(11 downto 0);
        b   : in  bit_vector(11 downto 0);
        o_and, o_or, o_xor, o_nand, o_nor, o_xnor, o_not : out bit_vector(11 downto 0) );
end entity;

architecture rtl of bitvec_dut is
    signal ra, rb : bit_vector(11 downto 0);
begin
    p_ff : process (clk) is
    begin
        if clk = '1' and clk'event then
            ra <= a;
            rb <= b;
        end if;
    end process;

    p_comb : process (ra, rb) is
    begin
        o_and  <= ra and rb;
        o_or   <= ra or rb;
        o_xor  <= ra xor rb;
        o_nand <= ra nand rb;
        o_nor  <= ra nor rb;
        o_xnor <= ra xnor rb;
        o_not  <= not ra;
    end process;
end architecture;

entity vhdl_island_bitvec_logic1 is
end entity;

architecture test of vhdl_island_bitvec_logic1 is
    signal clk : bit := '0';
    signal a, b : bit_vector(11 downto 0) := (others => '0');
    signal o_and, o_or, o_xor, o_nand, o_nor, o_xnor, o_not : bit_vector(11 downto 0);
begin
    dut : entity work.bitvec_dut
        port map (clk, a, b, o_and, o_or, o_xor, o_nand, o_nor, o_xnor, o_not);

    stim : process is
        type pair_t is record x, y : bit_vector(11 downto 0); end record;
        type pairs_t is array (natural range <>) of pair_t;
        constant PAIRS : pairs_t := (
            (x"000", x"000"), (x"fff", x"000"), (x"a5c", x"3f1"),
            (x"0f0", x"f0f"), (x"801", x"7fe"), (x"123", x"123") );
    begin
        for i in PAIRS'range loop
            a <= PAIRS(i).x;
            b <= PAIRS(i).y;
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
            assert o_and  = (PAIRS(i).x and PAIRS(i).y) report "and " & integer'image(i) severity failure;
            assert o_or   = (PAIRS(i).x or PAIRS(i).y) report "or " & integer'image(i) severity failure;
            assert o_xor  = (PAIRS(i).x xor PAIRS(i).y) report "xor " & integer'image(i) severity failure;
            assert o_nand = (PAIRS(i).x nand PAIRS(i).y) report "nand " & integer'image(i) severity failure;
            assert o_nor  = (PAIRS(i).x nor PAIRS(i).y) report "nor " & integer'image(i) severity failure;
            assert o_xnor = (PAIRS(i).x xnor PAIRS(i).y) report "xnor " & integer'image(i) severity failure;
            assert o_not  = (not PAIRS(i).x) report "not " & integer'image(i) severity failure;
        end loop;
        wait;
    end process;
end architecture;
