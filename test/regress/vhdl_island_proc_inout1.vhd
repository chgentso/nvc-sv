library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_proc_inout1 is
end entity;

architecture test of vhdl_island_proc_inout1 is
    type xer_t is record
        ca   : std_ulogic;
        ca32 : std_ulogic;
        ov   : std_ulogic;
        so   : std_ulogic;
    end record;

    type wb_t is record
        data : std_ulogic_vector(7 downto 0);
        xerc : xer_t;
    end record;

    type stage_t is record
        e     : wb_t;
        valid : std_ulogic;
    end record;

    constant stage_init : stage_t := (
        e     => (data => (others => '0'),
                  xerc => (ca => '0', ca32 => '0', ov => '0', so => '0')),
        valid => '0'
    );

    procedure set_carry(e       : inout wb_t;
                        carry32 : in std_ulogic;
                        carry   : in std_ulogic) is
    begin
        e.xerc.ca32 := carry32;
        e.xerc.ca   := carry;
    end procedure;

    procedure set_ov(e  : inout wb_t;
                     ov : in std_ulogic) is
    begin
        e.xerc.ov := ov;
        if ov = '1' then
            e.xerc.so := '1';
        end if;
    end procedure;

    procedure bump_byte(x : inout std_ulogic_vector(7 downto 0);
                        k : in unsigned(7 downto 0)) is
    begin
        x := std_ulogic_vector(unsigned(x) + k);
    end procedure;

    signal clk    : std_logic := '0';
    signal rst    : std_logic := '1';
    signal en     : std_ulogic := '0';
    signal ov_in  : std_ulogic := '0';
    signal add_in : unsigned(7 downto 0) := (others => '0');
    signal r, rin : stage_t := stage_init;
    signal done   : boolean := false;
begin
    p_comb : process (all) is
        variable v : stage_t;
    begin
        v := r;
        v.valid := en;
        if en = '1' then
            bump_byte(v.e.data, add_in);
            set_carry(v.e, add_in(0), add_in(1));
            set_ov(v.e, ov_in);
        end if;
        rin <= v;
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
        assert r.e.data = x"00" and r.e.xerc.so = '0' severity failure;

        rst <= '0';
        en <= '1';
        add_in <= to_unsigned(5, 8); -- bit0=1, bit1=0
        ov_in <= '1';
        wait until rising_edge(clk);
        wait for 1 ns;
        assert r.valid = '1' and r.e.data = x"05" severity failure;
        assert r.e.xerc.ca32 = '1' and r.e.xerc.ca = '0'
            and r.e.xerc.ov = '1' and r.e.xerc.so = '1' severity failure;

        add_in <= to_unsigned(2, 8); -- bit0=0, bit1=1
        ov_in <= '0';                -- sticky so must stay '1'
        wait until rising_edge(clk);
        wait for 1 ns;
        assert r.e.data = x"07" severity failure;
        assert r.e.xerc.ca32 = '0' and r.e.xerc.ca = '1'
            and r.e.xerc.ov = '0' and r.e.xerc.so = '1' severity failure;

        done <= true;
        wait;
    end process;
end architecture;
