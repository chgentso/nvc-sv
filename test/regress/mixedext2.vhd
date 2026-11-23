-- VHDL external signal types into Verilog nets and variables:
-- std_logic, std_ulogic, std_logic_vector, std_ulogic_vector, signed,
-- unsigned, bit, bit_vector, boolean, integer, natural, real;
-- read, 'event / wait on, and force / release.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixedext2 is
end entity;

architecture tb of mixedext2 is
    component mixedext2_dut is
        port (
            tick : in  std_logic;
            iobs : out std_logic_vector(31 downto 0);
            bobs : out std_logic
        );
    end component;

    signal tick : std_logic := '0';
    signal iobs : std_logic_vector(31 downto 0);
    signal bobs : std_logic;
    signal int_ev_cnt  : integer := 0;
    signal real_ev_cnt : integer := 0;
begin
    u_dut : mixedext2_dut port map (tick => tick, iobs => iobs, bobs => bobs);

    p_watch_int : process is
        alias sv_i is << signal .mixedext2.u_dut.i_var : integer >>;
    begin
        wait on sv_i;
        assert sv_i'event;
        int_ev_cnt <= int_ev_cnt + 1;
    end process;

    p_watch_real : process is
        alias sv_r is << signal .mixedext2.u_dut.r_var : real >>;
    begin
        wait on sv_r;
        assert sv_r'event;
        real_ev_cnt <= real_ev_cnt + 1;
    end process;

    p_main : process is
        alias sv_sl   is << signal .mixedext2.u_dut.w_bit   : std_logic >>;
        alias sv_sul  is << signal .mixedext2.u_dut.l_bit   : std_ulogic >>;
        alias sv_slv  is << signal .mixedext2.u_dut.w_vec   : std_logic_vector(7 downto 0) >>;
        alias sv_sulv is << signal .mixedext2.u_dut.l_vec   : std_ulogic_vector(7 downto 0) >>;
        alias sv_sgn  is << signal .mixedext2.u_dut.s_vec   : signed(7 downto 0) >>;
        alias sv_usgn is << signal .mixedext2.u_dut.u_vec   : unsigned(7 downto 0) >>;
        alias sv_bit  is << signal .mixedext2.u_dut.b_bit   : bit >>;
        alias sv_bv   is << signal .mixedext2.u_dut.b_vec   : bit_vector(3 downto 0) >>;
        alias sv_bool is << signal .mixedext2.u_dut.flag    : boolean >>;
        alias sv_int  is << signal .mixedext2.u_dut.i_var   : integer >>;
        alias sv_nat  is << signal .mixedext2.u_dut.n_var   : natural >>;
        alias sv_real is << signal .mixedext2.u_dut.r_var   : real >>;
    begin
        wait for 5 ns;
        assert sv_sl = '1';
        assert sv_sul = '0';
        assert sv_slv = x"A5";
        assert sv_sulv = x"3C";
        assert sv_sgn = to_signed(-12, 8);
        assert sv_usgn = to_unsigned(200, 8);
        assert sv_bit = '1';
        assert sv_bv = "1010";
        assert sv_bool = true;
        assert sv_int = -42;
        assert sv_nat = 77;
        assert abs(sv_real - 1.5) < 0.001;

        -- Step Verilog DUT
        tick <= '1';
        wait for 5 ns;
        assert sv_int = -41;
        assert sv_bool = false;
        assert abs(sv_real - 2.25) < 0.001;
        assert int_ev_cnt >= 1;
        assert real_ev_cnt >= 1;

        -- Force integer, boolean, real, signed, bit_vector, and a net (w_vec)
        sv_int  <= force 12345;
        sv_bool <= force true;
        sv_real <= force 9.5;
        sv_sgn  <= force to_signed(-5, 8);
        sv_bv   <= force "0110";
        sv_slv  <= force x"FF";
        wait for 1 ns;
        assert sv_int = 12345;
        assert iobs = std_logic_vector(to_signed(12345, 32));
        assert sv_bool = true;
        assert bobs = '1';
        assert abs(sv_real - 9.5) < 0.001;
        assert sv_sgn = to_signed(-5, 8);
        assert sv_bv = "0110";
        assert sv_slv = x"FF";

        -- Release: net returns to continuous driver (0xA5); procedural
        -- variables keep forced value until next procedural assignment (IEEE 1800 10.6.2)
        sv_int  <= release;
        sv_bool <= release;
        sv_real <= release;
        sv_sgn  <= release;
        sv_bv   <= release;
        sv_slv  <= release;
        tick <= '0';
        wait for 5 ns;
        assert sv_slv = x"A5";
        tick <= '1';
        wait for 5 ns;
        assert sv_int = 12346;
        assert sv_bool = false;
        assert abs(sv_real - 10.25) < 0.001;

        std.env.finish;
    end process;
end architecture;
