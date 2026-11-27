entity sig_rec_agg1 is
end entity;

architecture test of sig_rec_agg1 is
    type rec_t is record
        b : bit;
        v : bit_vector(1 to 2);
    end record;

    procedure check(signal r : rec_t) is
    begin
        assert r.b = '1' and r.v = "10";
    end procedure;

    procedure fwd(signal b : bit; signal v : bit_vector) is
    begin
        check(r.b => b, r.v => v);
    end procedure;

    signal sb : bit := '1';
    signal sv : bit_vector(1 to 2) := "10";
begin
    process is
    begin
        wait for 1 ns;
        fwd(sb, sv);
        wait;
    end process;
end architecture;

entity sig_rec_agg1_bad is
end entity;

architecture test of sig_rec_agg1_bad is
    type rec_t is record
        b : bit;
        v : bit_vector(1 to 3);
    end record;

    procedure check(signal r : rec_t) is
    begin
        assert r.b = '1';
    end procedure;

    procedure fwd(signal b : bit; signal v : bit_vector) is
    begin
        check(r.b => b, r.v => v);
    end procedure;

    signal sb : bit := '1';
    signal sv : bit_vector(1 to 2) := "10";
begin
    process is
    begin
        wait for 1 ns;
        fwd(sb, sv);
        wait;
    end process;
end architecture;
