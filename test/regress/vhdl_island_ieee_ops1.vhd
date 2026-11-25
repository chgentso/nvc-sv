library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package ieee_ops_pkg is
    function leading_ones(v : std_logic_vector) return unsigned;
    function parity_skip_lsb(v : std_logic_vector) return std_logic;
end package;

package body ieee_ops_pkg is
    function leading_ones(v : std_logic_vector) return unsigned is
        variable cnt : unsigned(3 downto 0) := (others => '0');
    begin
        for i in v'range loop
            if v(i) = '0' then
                exit;
            end if;
            cnt := cnt + 1;
        end loop;
        return cnt;
    end function;

    function parity_skip_lsb(v : std_logic_vector) return std_logic is
        variable p : std_logic := '0';
    begin
        for i in v'range loop
            if i = v'low then
                next;
            end if;
            p := p xor v(i);
        end loop;
        return p;
    end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.ieee_ops_pkg.all;

entity vhdl_island_ieee_ops1 is
end entity;

architecture test of vhdl_island_ieee_ops1 is
    signal clk       : std_logic := '0';
    signal en        : std_logic := '0';
    signal a         : unsigned(7 downto 0) := (others => '0');
    signal b         : unsigned(7 downto 0) := (others => '0');
    signal sa        : signed(7 downto 0) := (others => '0');
    signal vec_in    : std_logic_vector(7 downto 0) := (others => '0');
    signal sum_out   : unsigned(8 downto 0) := (others => '0');
    signal sres_out  : signed(11 downto 0) := (others => '0');
    signal shl_out   : unsigned(7 downto 0) := (others => '0');
    signal shr_out   : signed(7 downto 0) := (others => '0');
    signal lo_out    : unsigned(3 downto 0) := (others => '0');
    signal par_out   : std_logic := '0';
    signal cmp_out   : std_logic := '0';
    signal isx_out   : std_logic := '0';
    signal cat_out   : std_logic_vector(7 downto 0) := (others => '0');
    signal done      : boolean := false;
begin
    p_comb : process (all) is
        variable vsum : unsigned(8 downto 0);
    begin
        vsum     := resize(a, 9) + resize(b, 9);
        sum_out  <= vsum;
        sres_out <= resize(sa, 12);
        shl_out  <= shift_left(a, 2);
        shr_out  <= shift_right(sa, 2);
        lo_out   <= leading_ones(vec_in);
        par_out  <= parity_skip_lsb(vec_in);
        if (?? en) and (a >= b) then
            cmp_out <= '1';
        else
            cmp_out <= '0';
        end if;
        if is_x(vec_in) then
            isx_out <= '1';
        else
            isx_out <= '0';
        end if;
        cat_out <= vec_in(3 downto 0) & std_logic_vector(a(3 downto 0));
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            if ?? en then
                a <= a + 1;
            end if;
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

    p_check : process is
    begin
        en     <= '1';
        b      <= to_unsigned(2, 8);
        sa     <= to_signed(-16, 8);
        vec_in <= "11101011";
        wait until rising_edge(clk);
        wait for 1 ns;
        -- a became 1, b = 2
        assert sum_out = to_unsigned(3, 9) severity failure;
        assert sres_out = to_signed(-16, 12) severity failure;
        assert shl_out = to_unsigned(4, 8) severity failure;
        assert shr_out = to_signed(-4, 8) severity failure;
        assert lo_out = to_unsigned(3, 4) severity failure;
        assert par_out = '1' severity failure;  -- bits 7..1 = 1,1,1,0,1,0,1 -> 5 ones -> '1'
        assert cmp_out = '0' severity failure;
        assert isx_out = '0' severity failure;
        assert cat_out = "10110001" severity failure;

        wait until rising_edge(clk);
        wait for 1 ns;
        -- a became 2, b = 2 -> a >= b is true
        assert sum_out = to_unsigned(4, 9) severity failure;
        assert cmp_out = '1' severity failure;

        vec_in <= "11X00000";
        wait for 1 ns;
        assert isx_out = '1' severity failure;

        done <= true;
        wait;
    end process;
end architecture;
