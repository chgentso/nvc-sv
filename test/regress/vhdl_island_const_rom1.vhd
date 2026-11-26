library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_const_rom1 is
end entity;

architecture test of vhdl_island_const_rom1 is
    type rom16x8_t is array (0 to 15) of std_ulogic_vector(7 downto 0);
    constant sbox_c : rom16x8_t := (
        x"63", x"7C", x"77", x"7B", x"F2", x"6B", x"6F", x"C5",
        others => (others => '0')
    );

    type op_t is (OP_NOP, OP_ADD, OP_PRIV, OP_HALT);
    type priv_t is (USER, SUPER);
    type priv_lut_t is array (op_t) of priv_t;
    constant op_priv_c : priv_lut_t := (
        OP_PRIV => SUPER,
        OP_HALT => SUPER,
        others  => USER
    );

    signal clk      : std_logic := '0';
    signal addr     : unsigned(3 downto 0) := (others => '0');
    signal op_in    : op_t := OP_NOP;
    signal rom_c    : std_ulogic_vector(7 downto 0) := (others => '0');
    signal rom_q    : std_ulogic_vector(7 downto 0) := (others => '0');
    signal rom_k_q  : std_ulogic_vector(7 downto 0) := (others => '0');
    signal priv_q   : priv_t := USER;
    signal done     : boolean := false;
begin
    p_comb : process (all) is
    begin
        rom_c <= sbox_c(to_integer(addr));
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            rom_q   <= rom_c;
            rom_k_q <= sbox_c(3);
            priv_q  <= op_priv_c(op_in);
        end if;
    end process;

    p_clk : process is
    begin
        while not done loop
            clk <= '0';
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
        end loop;
        wait;
    end process;

    p_stim : process is
    begin
        addr  <= to_unsigned(0, 4);
        op_in <= OP_ADD;
        wait until rising_edge(clk);
        wait for 1 ns;
        assert rom_q = x"63" severity failure;
        assert rom_k_q = x"7B" severity failure;
        assert priv_q = USER severity failure;

        addr  <= to_unsigned(5, 4);
        op_in <= OP_PRIV;
        wait until rising_edge(clk);
        wait for 1 ns;
        assert rom_q = x"6B" severity failure;
        assert priv_q = SUPER severity failure;

        addr  <= to_unsigned(12, 4);
        op_in <= OP_HALT;
        wait until rising_edge(clk);
        wait for 1 ns;
        assert rom_q = x"00" severity failure;
        assert priv_q = SUPER severity failure;

        done <= true;
        wait;
    end process;
end architecture;
