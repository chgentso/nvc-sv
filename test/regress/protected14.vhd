-- LCS-2016-014: composites of protected types.  Arrays and records whose
-- elements are protected types; each noncomposite subelement is an
-- independent protected object (IEEE 1076-2019 5.3.1, 6.4.2.4, 8.3,
-- 4.2.2.2).

package counters is
    type counter_t is protected
        procedure add (n : integer := 1);
        impure function get return integer;
    end protected;

    type counter_vec is array (natural range <>) of counter_t;
    type counter_pair is record
        x, y : counter_t;
    end record;
    type pair_vec is array (1 to 3) of counter_pair;
    type counter_mat is array (1 to 2, 1 to 3) of counter_t;

    procedure add_all (variable v : inout counter_vec; n : integer);
    impure function sum (variable v : inout counter_vec) return integer;
end package;

package body counters is
    type counter_t is protected body
        variable value : integer := 0;

        procedure add (n : integer := 1) is
        begin
            value := value + n;
        end procedure;

        impure function get return integer is
        begin
            return value;
        end function;
    end protected body;

    procedure add_all (variable v : inout counter_vec; n : integer) is
    begin
        for i in v'range loop
            v(i).add(n * i);
        end loop;
    end procedure;

    impure function sum (variable v : inout counter_vec) return integer is
        variable result : integer := 0;
    begin
        for i in v'range loop
            result := result + v(i).get;
        end loop;
        return result;
    end function;
end package body;

-------------------------------------------------------------------------------

use work.counters.all;

package counters_shared is
    -- Package-level shared variable of a composite of protected types
    shared variable pkg_counters : counter_vec(0 to 3);
end package;

-------------------------------------------------------------------------------

use work.counters.all;
use work.counters_shared.all;

entity protected14 is
end entity;

architecture test of protected14 is
    shared variable sv : counter_vec(1 to 4);
    shared variable sp : counter_pair;

    -- Protected type with a private composite of protected types
    type bank_t is protected
        procedure deposit (slot : natural; n : integer);
        impure function total return integer;
    end protected;

    type bank_t is protected body
        variable slots : counter_vec(0 to 2);

        procedure deposit (slot : natural; n : integer) is
        begin
            slots(slot).add(n);
        end procedure;

        impure function total return integer is
        begin
            return sum(slots);
        end function;
    end protected body;

    shared variable bank : bank_t;
begin

    -- Two processes updating different elements of the same shared
    -- composite
    p1: process is
    begin
        for i in 1 to 5 loop
            sv(1).add;
            sp.x.add(2);
            wait for 1 ns;
        end loop;
        wait;
    end process;

    p2: process is
    begin
        for i in 1 to 3 loop
            sv(4).add(10);
            sp.y.add(3);
            wait for 1 ns;
        end loop;
        wait;
    end process;

    check: process is
        variable v : counter_vec(0 to 2);
        variable pv : pair_vec;
        variable m : counter_mat;
        variable r : counter_pair;
    begin
        -- Independent elements of a local variable
        v(0).add(5);
        v(2).add;
        v(2).add;
        assert v(0).get = 5;
        assert v(1).get = 0;
        assert v(2).get = 2;
        assert v'length = 3;

        -- Composite of protected type passed as inout parameter
        add_all(v, 3);                  -- Adds 0, 3, 6
        assert v(0).get = 5;
        assert v(1).get = 3;
        assert v(2).get = 8;
        assert sum(v) = 16;

        -- Slice of a composite of protected types
        add_all(v(1 to 2), 1);          -- Adds 1, 2
        assert v(1).get = 4;
        assert v(2).get = 10;

        -- Records and nested composites
        r.x.add(7);
        assert r.x.get = 7 and r.y.get = 0;
        pv(2).y.add(4);
        pv(3).x.add(-1);
        assert pv(1).x.get = 0 and pv(1).y.get = 0;
        assert pv(2).x.get = 0 and pv(2).y.get = 4;
        assert pv(3).x.get = -1 and pv(3).y.get = 0;

        -- Multidimensional arrays
        for i in 1 to 2 loop
            for j in 1 to 3 loop
                m(i, j).add(i * 10 + j);
            end loop;
        end loop;
        assert m(1, 1).get = 11;
        assert m(2, 3).get = 23;

        -- Package-level shared composite
        pkg_counters(3).add(42);
        assert pkg_counters(3).get = 42;
        assert pkg_counters(0).get = 0;

        -- Composite private to a protected type
        bank.deposit(0, 1);
        bank.deposit(2, 100);
        assert bank.total = 101;

        wait for 10 ns;

        assert sv(1).get = 5;
        assert sv(2).get = 0;
        assert sv(3).get = 0;
        assert sv(4).get = 30;
        assert sum(sv) = 35;
        assert sp.x.get = 10;
        assert sp.y.get = 9;

        wait;
    end process;

end architecture;
