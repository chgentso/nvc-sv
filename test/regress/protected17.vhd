-- LCS-2016-014a: access types designating composites of protected types
-- (IEEE 1076-2019 5.4.1, 9.3.7, 14.6).

entity protected17 is
end entity;

architecture test of protected17 is
    type counter_t is protected
        procedure inc;
        impure function get return natural;
    end protected;

    type counter_t is protected body
        variable n : natural := 0;
        procedure inc is
        begin
            n := n + 1;
        end procedure;
        impure function get return natural is
        begin
            return n;
        end function;
    end protected body;

    type counter_vec is array (natural range <>) of counter_t;
    type counter_vec_ptr is access counter_vec;

    type counter_rec is record
        a, b : counter_t;
    end record;
    type counter_rec_ptr is access counter_rec;

begin

    p1: process is
        variable vp : counter_vec_ptr;
        variable rp : counter_rec_ptr;
    begin
        -- Allocator for an unconstrained array of protected type
        vp := new counter_vec(1 to 3);
        assert vp'length = 3;
        assert vp.all'length = 3;
        vp(1).inc;
        vp.all(3).inc;
        vp(3).inc;
        assert vp(1).get = 1;
        assert vp(2).get = 0;
        assert vp(3).get = 2;
        deallocate(vp);
        assert vp = null;

        rp := new counter_rec;
        rp.a.inc;
        rp.all.b.inc;
        rp.b.inc;
        assert rp.a.get = 1;
        assert rp.b.get = 2;
        deallocate(rp);

        wait;
    end process;

end architecture;
