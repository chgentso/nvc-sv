-- LCS-2016-016: anonymous types on ports.  Each port declared with an
-- anonymous type indication has an implicit formal generic type whose
-- actual is the subtype of the associated actual port (IEEE 1076-2019
-- 6.5.2, 6.5.7.1).

entity anon_reg is
    port (
        clk : in bit;
        d   : in type is private;
        q   : out d'subtype );
end entity;

architecture test of anon_reg is
begin
    process (clk) is
    begin
        if clk'event and clk = '1' then
            q <= d;
        end if;
    end process;
end architecture;

-------------------------------------------------------------------------------

entity anon_add is
    port (
        a : in type is range <>;
        y : out a'subtype );
end entity;

architecture test of anon_add is
begin
    y <= a + 3;
end architecture;

-------------------------------------------------------------------------------

entity anon_pos is
    port (
        e : in type is (<>);
        p : out integer;
        w : out integer );
end entity;

architecture test of anon_pos is
    subtype e_t is e'subtype;
begin
    p <= e_t'pos(e) - e_t'pos(e_t'low);
    w <= e_t'pos(e_t'high) - e_t'pos(e_t'low) + 1;
end architecture;

-------------------------------------------------------------------------------

entity anontype1 is
end entity;

architecture test of anontype1 is
    type state_t is (idle, run, done, error);
    type rec_t is record
        x : integer;
        s : state_t;
    end record;
    subtype small_t is integer range -10 to 10;

    -- Component declaration with an anonymous port type
    component anon_reg is
        port (
            clk : in bit;
            d   : in type is private;
            q   : out d'subtype );
    end component;

    signal clk   : bit := '0';
    signal bv_d  : bit_vector(7 downto 0);
    signal bv_q  : bit_vector(7 downto 0);
    signal r_d   : rec_t;
    signal r_q   : rec_t;
    signal c_d   : character := 'x';
    signal c_q   : character;
    signal i_a   : small_t := 4;
    signal i_y   : small_t;
    signal st    : state_t := run;
    signal st_p  : integer;
    signal st_w  : integer;
    signal ch_p  : integer;
    signal ch_w  : integer;
begin

    u1: entity work.anon_reg port map (clk, bv_d, bv_q);
    u2: entity work.anon_reg port map (clk => clk, d => r_d, q => r_q);
    u3: component anon_reg port map (clk, c_d, c_q);
    u4: entity work.anon_add port map (i_a, i_y);
    u5: entity work.anon_pos port map (st, st_p, st_w);
    u6: entity work.anon_pos port map (e => c_d, p => ch_p, w => ch_w);

    stim: process is
    begin
        bv_d <= X"A5";
        r_d <= (x => 42, s => done);
        c_d <= 'q';
        wait for 1 ns;
        assert bv_q = X"00";
        assert r_q = (integer'low, idle);
        assert i_y = 7;
        assert st_p = 1;
        assert st_w = 4;
        assert ch_p = character'pos('q');
        assert ch_w = 256;

        clk <= '1';
        wait for 1 ns;
        assert bv_q = X"A5";
        assert r_q = (42, done);
        assert c_q = 'q';

        i_a <= -10;
        st <= error;
        wait for 1 ns;
        assert i_y = -7;
        assert st_p = 3;

        wait;
    end process;

end architecture;
