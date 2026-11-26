library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_arr2d_assign1 is
end entity;

architecture test of vhdl_island_arr2d_assign1 is
    type bank_t is array (3 downto 0) of std_ulogic_vector(7 downto 0);
    type rec_t is record
        tag  : std_ulogic_vector(3 downto 0);
        bank : bank_t;
    end record;

    signal clk    : std_logic := '0';
    signal rstn   : std_logic := '0';
    signal we     : std_logic := '0';
    signal widx   : natural range 0 to 3 := 0;
    signal wdata  : std_ulogic_vector(7 downto 0) := (others => '0');
    signal regs   : bank_t := (others => (others => '1'));
    signal r_in   : rec_t := (tag => (others => '0'), bank => (others => (others => '0')));
    signal r_q    : rec_t := (tag => (others => '0'), bank => (others => (others => '0')));
    signal done   : boolean := false;
begin
    p_comb : process (all) is
    begin
        r_in.tag  <= x"A";
        r_in.bank <= regs;
    end process;

    p_ff : process (rstn, clk) is
    begin
        if rstn = '0' then
            regs <= (others => (others => '0'));
            r_q  <= (tag => x"5", bank => (0 => x"11", 1 to 2 => x"22", others => x"33"));
        elsif rising_edge(clk) then
            if we = '1' then
                regs(widx) <= wdata;
            end if;
            r_q <= r_in;
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
        rstn <= '0';
        wait for 3 ns;
        assert regs(0) = x"00" and regs(3) = x"00" severity failure;
        assert r_q.tag = x"5" severity failure;
        assert r_q.bank(0) = x"11" and r_q.bank(1) = x"22"
            and r_q.bank(2) = x"22" and r_q.bank(3) = x"33" severity failure;

        rstn  <= '1';
        we    <= '1';
        widx  <= 2;
        wdata <= x"BE";
        wait until rising_edge(clk);
        wait for 1 ns;
        assert regs(2) = x"BE" and regs(0) = x"00" severity failure;
        assert r_q.tag = x"A" and r_q.bank(2) = x"00" severity failure;

        we <= '0';
        wait until rising_edge(clk);
        wait for 1 ns;
        assert r_q.bank(2) = x"BE" and r_q.bank(1) = x"00" severity failure;

        done <= true;
        wait;
    end process;
end architecture;
