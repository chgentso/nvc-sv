library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity slice_lane is
    generic (
        W  : natural := 8;
        ID : natural := 0
    );
    port (
        clk  : in  std_logic;
        din  : in  std_logic_vector(W - 1 downto 0);
        dout : out std_logic_vector(W - 1 downto 0)
    );
end entity;

architecture rtl of slice_lane is
    signal reg_v  : std_logic_vector(W - 1 downto 0) := (others => '0');
    signal next_v : std_logic_vector(W - 1 downto 0);
begin
    p_comb : process (all) is
    begin
        next_v <= std_logic_vector(unsigned(din) + to_unsigned(ID, W));
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            reg_v <= next_v;
        end if;
    end process;

    dout <= reg_v;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_slices1 is
end entity;

architecture test of vhdl_island_slices1 is
    type lane_rec_t is record
        val : std_logic_vector(7 downto 0);
        en  : std_logic;
    end record;
    type lane_arr_t is array (0 to 3) of lane_rec_t;
    type mem4x8_t   is array (0 to 3) of std_logic_vector(7 downto 0);

    signal clk       : std_logic := '0';
    signal sel       : integer range 0 to 3 := 0;
    signal din       : std_logic_vector(7 downto 0) := (others => '0');
    signal lanes     : lane_arr_t := (others => (val => (others => '0'), en => '0'));
    signal mem2d     : mem4x8_t := (others => (others => '0'));
    signal word32    : std_logic_vector(31 downto 0) := (others => '0');
    signal word_out  : std_logic_vector(7 downto 0) := (others => '0');
    signal split_bus : std_logic_vector(7 downto 0) := (others => '0');
    signal gen_out   : mem4x8_t;
    signal done      : boolean := false;
begin
    -- Cloned for-generate instances with generics
    g_lanes : for i in 0 to 3 generate
        u_lane : entity work.slice_lane
            generic map (
                W  => 8,
                ID => i + 1
            )
            port map (
                clk  => clk,
                din  => din,
                dout => gen_out(i)
            );
    end generate;

    -- 1D record array + 2D array + constant-width dynamic-offset slice target
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            lanes(0).val <= din;
            lanes(0).en  <= '1';
            lanes(1).val <= std_logic_vector(unsigned(din) + 1);
            lanes(1).en  <= '1';
            mem2d(sel)   <= din;
            word32(sel * 8 + 7 downto sel * 8) <= din;
        end if;
    end process;

    -- Constant-width dynamic-offset slice read + disjoint self-read/write ranges
    p_comb : process (all) is
    begin
        word_out <= word32(sel * 8 + 7 downto sel * 8);
        split_bus(7 downto 4) <= not split_bus(3 downto 0);
    end process;

    p_drive_low : process (all) is
    begin
        split_bus(3 downto 0) <= lanes(0).val(3 downto 0);
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
        for idx in 0 to 3 loop
            sel <= idx;
            din <= std_logic_vector(to_unsigned(16#20# + idx * 3, 8));
            wait until rising_edge(clk);
            wait for 1 ns;

            assert lanes(0).en = '1' severity failure;
            assert lanes(0).val = din severity failure;
            assert lanes(1).val = std_logic_vector(unsigned(din) + 1) severity failure;
            assert mem2d(idx) = din severity failure;
            assert word_out = din severity failure;
            assert split_bus(7 downto 4) = not din(3 downto 0) severity failure;

            for g in 0 to 3 loop
                assert gen_out(g) = std_logic_vector(unsigned(din) + to_unsigned(g + 1, 8))
                    severity failure;
            end loop;
        end loop;

        -- Verify earlier dynamic slices persisted in word32
        assert word32(7 downto 0)   = x"20" severity failure;
        assert word32(15 downto 8)  = x"23" severity failure;
        assert word32(23 downto 16) = x"26" severity failure;
        assert word32(31 downto 24) = x"29" severity failure;

        done <= true;
        wait;
    end process;
end architecture;
