library ieee;
use ieee.std_logic_1164.all;

entity extforce_dut is
    port (
        clk   : in  std_logic;
        we    : in  std_logic;
        din   : in  std_logic_vector(7 downto 0);
        dout  : out std_logic_vector(7 downto 0);
        ack   : out std_logic
    );
end entity;

architecture rtl of extforce_dut is
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            ack <= '0';
            if we = '1' then
                dout <= din;
                ack  <= '1';
            end if;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity vhdl_island_extforce1 is
end entity;

architecture test of vhdl_island_extforce1 is
    signal clk  : std_logic := '0';
    signal we   : std_logic := '0';
    signal din  : std_logic_vector(7 downto 0) := x"25";
    signal dout : std_logic_vector(7 downto 0);
    signal ack  : std_logic;

    signal dout_events : integer := 0;
    signal ack_events  : integer := 0;
    signal watch_en    : boolean := false;
begin
    dut : entity work.extforce_dut
        port map (clk => clk, we => we, din => din, dout => dout, ack => ack);

    p_mon : process (dout, ack) is
    begin
        if watch_en then
            if dout'event and dout'last_value /= "UUUUUUUU" then
                dout_events <= dout_events + 1;
            end if;
            if ack'event and ack'last_value /= 'U' then
                ack_events <= ack_events + 1;
            end if;
        end if;
    end process;

    p_stim : process is
        alias dut_dout is << signal dut.dout : std_logic_vector(7 downto 0) >>;
        alias dut_ack  is << signal dut.ack  : std_logic >>;
    begin
        -- Load initial value x"25" into dout and '1' into ack
        we <= '1';
        wait for 5 ns;
        clk <= '1';
        wait for 5 ns;
        clk <= '0';
        we <= '0';
        wait for 5 ns;
        clk <= '1';
        wait for 5 ns;
        clk <= '0';
        wait for 5 ns;

        assert dout = x"25" report "expected dout = x25";
        assert ack = '0' report "expected ack = 0";

        watch_en <= true;
        wait for 1 ns;

        -- Force both outputs across an intervening clock edge where dout is not written
        dut_dout <= force not dut_dout;
        dut_ack  <= force not dut_ack;
        wait for 5 ns;
        assert dout = x"DA" report "expected forced dout = xDA";
        assert ack = '1' report "expected forced ack = 1";
        assert dout_events = 1 report "expected 1 dout event on force";
        assert ack_events = 1 report "expected 1 ack event on force";

        -- Intervening rising clock edge (we='0': ack driven to '0', dout untouched)
        clk <= '1';
        wait for 5 ns;
        clk <= '0';
        wait for 5 ns;
        assert dout = x"DA" report "expected dout to remain forced";
        assert ack = '1' report "expected ack to remain forced";

        -- Release both: dout must restore x"25" and ack must restore '0'
        dut_dout <= release;
        dut_ack  <= release;
        wait for 0 ns;
        wait for 0 ns;
        wait for 1 ns;

        assert dout = x"25" report "expected released dout = x25";
        assert ack = '0' report "expected released ack = 0";
        assert dout_events = 2 report "expected 2 dout events after release";
        assert ack_events = 2 report "expected 2 ack events after release";
        wait;
    end process;
end architecture;
