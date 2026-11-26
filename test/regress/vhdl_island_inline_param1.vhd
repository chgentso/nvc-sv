library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package inline_param_pkg is
    function replicate_f(d : std_ulogic; n : natural) return std_ulogic_vector;
    function bit_rev_f(d : std_ulogic_vector) return std_ulogic_vector;
    function cond_mask_f(d : std_ulogic_vector; inv : std_ulogic) return std_ulogic_vector;
end package;

package body inline_param_pkg is
    function replicate_f(d : std_ulogic; n : natural) return std_ulogic_vector is
        variable v : std_ulogic_vector(n - 1 downto 0);
    begin
        v := (others => d);
        return v;
    end function;

    function bit_rev_f(d : std_ulogic_vector) return std_ulogic_vector is
        variable r : std_ulogic_vector(d'length - 1 downto 0);
    begin
        for i in 0 to d'length - 1 loop
            r((d'length - 1) - i) := d(d'low + i);
        end loop;
        return r;
    end function;

    function cond_mask_f(d : std_ulogic_vector; inv : std_ulogic) return std_ulogic_vector is
        variable tmp : std_ulogic_vector(d'range);
    begin
        if inv = '1' then
            tmp := not d;
            return tmp;
        else
            tmp := d;
            return tmp;
        end if;
    end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.inline_param_pkg.all;

entity vhdl_island_inline_param1 is
end entity;

architecture test of vhdl_island_inline_param1 is
    signal clk     : std_logic := '0';
    signal bit_in  : std_ulogic := '0';
    signal vec_in  : std_ulogic_vector(7 downto 0) := (others => '0');
    signal inv_in  : std_ulogic := '0';
    signal rep_c   : std_ulogic_vector(7 downto 0) := (others => '0');
    signal rev_c   : std_ulogic_vector(7 downto 0) := (others => '0');
    signal mask_c  : std_ulogic_vector(7 downto 0) := (others => '0');
    signal rep_q   : std_ulogic_vector(7 downto 0) := (others => '0');
    signal rev_q   : std_ulogic_vector(7 downto 0) := (others => '0');
    signal mask_q  : std_ulogic_vector(7 downto 0) := (others => '0');
    signal done    : boolean := false;
begin
    p_comb : process (all) is
    begin
        rep_c  <= replicate_f(bit_in, 8);
        rev_c  <= bit_rev_f(vec_in);
        mask_c <= cond_mask_f(vec_in, inv_in);
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            rep_q  <= rep_c;
            rev_q  <= rev_c;
            mask_q <= mask_c;
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
        bit_in <= '1';
        vec_in <= x"A3"; -- 1010_0011 -> bit_rev = 1100_0101 = x"C5"
        inv_in <= '0';
        wait until rising_edge(clk);
        wait for 1 ns;
        assert rep_q = x"FF" severity failure;
        assert rev_q = x"C5" severity failure;
        assert mask_q = x"A3" severity failure;

        bit_in <= '0';
        vec_in <= x"1F"; -- 0001_1111 -> bit_rev = 1111_1000 = x"F8"
        inv_in <= '1';
        wait until rising_edge(clk);
        wait for 1 ns;
        assert rep_q = x"00" severity failure;
        assert rev_q = x"F8" severity failure;
        assert mask_q = x"E0" severity failure;

        done <= true;
        wait;
    end process;
end architecture;
