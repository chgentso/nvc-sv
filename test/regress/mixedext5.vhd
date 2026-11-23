-- VHDL external constants referencing Verilog parameters (default and
-- overridden) and localparams (module-level and inside shared generate blocks).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixedext5 is
end entity;

architecture tb of mixedext5 is
    component mixedext5_dut is
        generic (
            DEPTH : integer := 8
        );
    end component;
begin
    u_def : mixedext5_dut;
    u_ovr : mixedext5_dut generic map (DEPTH => 16);

    process is
        alias p_def   is << constant .mixedext5.u_def.DEPTH     : integer >>;
        alias p_ovr   is << constant .mixedext5.u_ovr.DEPTH     : natural >>;
        alias p_neg   is << constant .mixedext5.u_def.NEG_VAL   : integer >>;
        alias p_real  is << constant .mixedext5.u_def.GAIN      : real >>;
        alias p_bool  is << constant .mixedext5.u_def.ENABLE    : boolean >>;
        alias p_bit   is << constant .mixedext5.u_def.FLAG      : std_logic >>;
        alias p_slv   is << constant .mixedext5.u_def.MASK      : std_logic_vector(7 downto 0) >>;
        alias p_bv    is << constant .mixedext5.u_def.NIB       : bit_vector(3 downto 0) >>;
        alias p_sgn   is << constant .mixedext5.u_def.OFFS      : signed(7 downto 0) >>;
        alias p_str   is << constant .mixedext5.u_def.TAG       : string >>;
        alias p_lpar  is << constant .mixedext5.u_ovr.DOUBLED   : integer >>;
        alias p_gen0  is << constant .mixedext5.u_def.gen_c(0).GP : integer >>;
        alias p_gen3  is << constant .mixedext5.u_def.gen_c(3).GP : integer >>;
    begin
        wait for 1 ns;
        assert p_def = 8;
        assert p_ovr = 16;
        assert p_neg = -25;
        assert abs(p_real - 3.25) < 0.001;
        assert p_bool = true;
        assert p_bit = '1';
        assert p_slv = x"C3";
        assert p_bv = "1011";
        assert p_sgn = to_signed(-7, 8);
        assert p_str = "hello";
        assert p_lpar = 32;
        assert p_gen0 = 1;
        assert p_gen3 = 10;
        std.env.finish;
    end process;
end architecture;
