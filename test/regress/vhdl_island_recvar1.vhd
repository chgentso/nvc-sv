library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package recvar_pkg is
    type flags_t is record
        ca   : std_ulogic;
        ov   : std_ulogic;
        mask : std_ulogic_vector(3 downto 0);
    end record;

    constant flags_init : flags_t := (
        ca   => '0',
        ov   => '0',
        mask => (others => '0')
    );

    type bank2_t is array (0 to 1) of std_ulogic_vector(7 downto 0);

    type stage_t is record
        valid : std_ulogic;
        addr  : std_ulogic_vector(15 downto 0);
        cnt   : integer range 0 to 15;
        fl    : flags_t;
        bank  : bank2_t;
    end record;

    constant stage_init : stage_t := (
        valid => '0',
        addr  => (others => '0'),
        cnt   => 0,
        fl    => flags_init,
        bank  => (others => (others => '0'))
    );

    function get_carry(f : flags_t) return std_ulogic;
end package;

package body recvar_pkg is
    function get_carry(f : flags_t) return std_ulogic is
    begin
        return f.ca or f.ov;
    end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.recvar_pkg.all;

entity vhdl_island_recvar1 is
end entity;

architecture test of vhdl_island_recvar1 is
    signal clk       : std_logic := '0';
    signal rst       : std_logic := '1';
    signal en        : std_ulogic := '0';
    signal mode_clr  : std_ulogic := '0';
    signal addr_in   : std_ulogic_vector(15 downto 0) := (others => '0');
    signal r, rin    : stage_t := stage_init;
    signal carry_out : std_ulogic := '0';
    signal eq_out    : std_ulogic := '0';
    signal done      : boolean := false;
begin
    -- Classic two-process VHDL combinational process with record local variable
    p_comb : process (all) is
        variable v  : stage_t;
        variable v2 : stage_t;
    begin
        if mode_clr = '1' then
            v := stage_init;
        else
            v := r;
            v.valid := en;
            if en = '1' then
                v.addr := addr_in;
                v.addr(3 downto 0) := x"5";
                v.fl.ca := addr_in(15);
                v.fl.ov := addr_in(14);
                v.fl.mask(1) := '1';
                v.bank(0) := addr_in(15 downto 8);
                v.bank(1) := addr_in(7 downto 0);
                if v.cnt < 15 then
                    v.cnt := v.cnt + 1;
                end if;
            end if;
        end if;

        v2 := v;
        if v2 = r then
            eq_out <= '1';
        else
            eq_out <= '0';
        end if;
        carry_out <= get_carry(v2.fl);
        rin <= v2;
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            if rst = '1' then
                r <= stage_init;
            else
                r <= rin;
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

    p_stim : process is
    begin
        rst <= '1';
        wait until rising_edge(clk);
        wait for 1 ns;
        assert r.valid = '0' and r.addr = x"0000" and r.cnt = 0 severity failure;
        assert eq_out = '1' severity failure;

        rst <= '0';
        en <= '1';
        addr_in <= x"82A0";
        wait for 1 ns;
        assert eq_out = '0' severity failure;
        assert carry_out = '1' severity failure;
        wait until rising_edge(clk);
        wait for 1 ns;
        assert r.valid = '1' severity failure;
        assert r.addr = x"82A5" severity failure;
        assert r.cnt = 1 severity failure;
        assert r.fl.ca = '1' and r.fl.ov = '0' and r.fl.mask = "0010" severity failure;
        assert r.bank(0) = x"82" and r.bank(1) = x"A0" severity failure;

        en <= '0';
        wait until rising_edge(clk);
        wait for 1 ns;
        assert r.valid = '0' and r.addr = x"82A5" and r.cnt = 1 severity failure;

        mode_clr <= '1';
        wait until rising_edge(clk);
        wait for 1 ns;
        assert r.valid = '0' and r.addr = x"0000" and r.cnt = 0
            and r.fl.ca = '0' and r.bank(0) = x"00" severity failure;

        done <= true;
        wait;
    end process;
end architecture;
