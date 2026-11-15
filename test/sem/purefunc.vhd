entity purefunc is
end entity;

architecture test of purefunc is
    type ptr is access integer;
    type holder is record
        p : ptr;
    end record;
    type holders is array (natural range <>) of holder;

    function f1 return ptr is                 -- Error
    begin
        return null;
    end function;

    function f2 return holder is              -- Error
    begin
        return (p => null);
    end function;

    function f3 return holders is             -- Error
    begin
        return (1 to 0 => (p => null));
    end function;

    impure function f4 return ptr is          -- OK
    begin
        return null;
    end function;

    pure function f5 return integer is        -- OK
    begin
        return 1;
    end function;
begin
end architecture;
