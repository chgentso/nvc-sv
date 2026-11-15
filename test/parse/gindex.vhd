entity gindex is
end entity;

architecture test of gindex is
    subtype idx3 is natural range 0 to 2;

    function f1 generic (type m_t is array (natural range <>, idx3) of bit)  -- Error
        parameter (m : m_t) return natural is
    begin
        return 0;
    end function;

    function f2 generic (type a_t is array (type is range <>.<>) of type is private)  -- Error
        parameter (a : a_t) return natural is
    begin
        return 0;
    end function;

    function f3 generic (type a_t is array (type is (<>)) of type is private)  -- OK
        parameter (a : a_t) return natural is
    begin
        return 0;
    end function;
begin
end architecture;
