package p_bad_var is
    variable v1 : integer := 1;          -- Error
end package;

package p_miss_outer is
    package inner is
        function f return integer;
    end package;
end package;

package body p_miss_outer is             -- Error: missing body for inner
end package body;

entity nestedpkg is
    package ent_p is
        constant C : integer;
        function f(x : integer) return integer;
    end package;
    package body ent_p is
        constant C : integer := 5;
        function f(x : integer) return integer is
        begin
            return x + C;
        end function;
    end package body;
end entity;

architecture rtl of nestedpkg is
    type pt_t is protected
        procedure tick;
    end protected;
    type pt_t is protected body
        package p_in_pt is
            variable ok_v : integer := 0;
            shared variable bad_sv : pt_t;       -- Error
            signal bad_sig : integer := 0;       -- Error
        end package;
        procedure tick is
        begin
            p_in_pt.ok_v := p_in_pt.ok_v + 1;
        end procedure;
    end protected body;

    package p_in_arch is
        variable bad_arch_v : integer := 0;      -- Error
        shared variable ok_sv : pt_t;
    end package;

    function fn(x : integer) return integer is
        package p_in_fn is
            variable local_v : integer := x;
            shared variable bad_fn_sv : pt_t;    -- Error
            signal bad_fn_sig : integer;         -- Error
        end package;
    begin
        return p_in_fn.local_v;
    end function;
begin
    proc: process is
        package p_in_proc is
            variable local_v : integer := 1;
            shared variable bad_proc_sv : pt_t;  -- Error
            signal bad_proc_sig : integer;       -- Error
        end package;
    begin
        p_in_proc.local_v := p_in_proc.local_v + 1;
        wait;
    end process;
end architecture;
