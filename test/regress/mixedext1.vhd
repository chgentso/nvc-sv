-- VHDL external names traversing Verilog for-generate, if-generate,
-- nested VHDL-in-Verilog-in-VHDL hierarchy, relative (^.) paths,
-- 'event / wait on, and force / release.
library ieee;
use ieee.std_logic_1164.all;

entity mixedext1_leaf is
    port (
        clk : in  std_logic;
        obs : out std_logic_vector(7 downto 0)
    );
end entity;

architecture rtl of mixedext1_leaf is
begin
    -- Relative upward path (^.) out of a VHDL child inside a Verilog
    -- generate block up to a sibling signal in the enclosing Verilog scope
    process is
        alias sib is << signal ^.lane_sig : std_logic_vector(7 downto 0) >>;
    begin
        wait on sib;
        assert sib'event;
        obs <= sib;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity mixedext1 is
end entity;

architecture tb of mixedext1 is
    component mixedext1_dut is
        port (
            clk : in  std_logic;
            din : in  std_logic_vector(7 downto 0);
            q0  : out std_logic_vector(7 downto 0);
            q2  : out std_logic_vector(7 downto 0);
            qo  : out std_logic_vector(7 downto 0)
        );
    end component;

    signal clk : std_logic := '0';
    signal din : std_logic_vector(7 downto 0) := x"10";
    signal q0, q2, qo : std_logic_vector(7 downto 0);
    signal ev_count : integer := 0;
begin
    u_dut : mixedext1_dut
        port map (clk => clk, din => din, q0 => q0, q2 => q2, qo => qo);

    -- Monitor 'event / wait on a Verilog signal inside a for-generate block
    p_watch : process is
        alias g2_sig is << signal .mixedext1.u_dut.gen_lane(2).lane_sig : std_logic_vector(7 downto 0) >>;
    begin
        wait on g2_sig;
        assert g2_sig'event;
        ev_count <= ev_count + 1;
    end process;

    p_main : process is
        alias g0_sig  is << signal .mixedext1.u_dut.gen_lane(0).lane_sig : std_logic_vector(7 downto 0) >>;
        alias g2_ext  is << signal .mixedext1.u_dut.\gen_lane[2]\.lane_sig : std_logic_vector(7 downto 0) >>;
        alias opt_sig is << signal .mixedext1.u_dut.gen_opt.opt_reg : std_logic_vector(7 downto 0) >>;
        alias leaf_obs is << signal .mixedext1.u_dut.gen_lane(2).u_leaf.obs : std_logic_vector(7 downto 0) >>;
    begin
        wait for 5 ns;
        clk <= '1';
        wait for 5 ns;
        -- gen_lane[0] adds 0 (0x10), gen_lane[2] adds 2 (0x12), gen_opt adds 5 (0x15)
        assert g0_sig = x"10" report "g0_sig mismatch";
        assert g2_ext = x"12" report "g2_ext mismatch";
        assert opt_sig = x"15" report "opt_sig mismatch";
        assert q2 = x"12" report "q2 mismatch";
        assert leaf_obs = x"12" report "relative path leaf_obs mismatch";
        assert ev_count >= 1 report "wait on / 'event did not fire";

        -- Force and release inside Verilog for-generate and if-generate blocks
        g2_ext <= force x"AA";
        opt_sig <= force x"55";
        wait for 1 ns;
        assert g2_ext = x"AA" report "g2_ext force mismatch";
        assert q2 = x"AA" report "q2 force propagation mismatch";
        assert opt_sig = x"55" report "opt_sig force mismatch";
        assert qo = x"55" report "qo force propagation mismatch";

        g2_ext <= release;
        opt_sig <= release;
        clk <= '0';
        din <= x"20";
        wait for 5 ns;
        clk <= '1';
        wait for 5 ns;
        assert g2_ext = x"22" report "g2_ext after release mismatch";
        assert q2 = x"22" report "q2 after release mismatch";
        assert opt_sig = x"25" report "opt_sig after release mismatch";
        assert qo = x"25" report "qo after release mismatch";
        assert ev_count >= 3 report "expected >=3 events on g2_sig";

        std.env.finish;
    end process;
end architecture;
