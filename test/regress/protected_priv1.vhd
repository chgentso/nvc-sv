-- Private subprograms declared inside a protected type body may use
-- access and file types in their parameter lists and return types
-- under VHDL-2008 (LRM 5.6.2 applies only to protected type declarations).

entity protected_priv1 is
end entity;

architecture test of protected_priv1 is
    type box_t is protected
        procedure push (constant v : in integer);
        impure function pop return integer;
    end protected;

    type box_t is protected body
        type node_t;
        type node_ptr is access node_t;
        type node_t is record
            val  : integer;
            next_node : node_ptr;
        end record;

        variable head : node_ptr := null;

        impure function alloc_node (constant v : in integer) return node_ptr;

        procedure unlink (variable p : inout node_ptr);

        impure function alloc_node (constant v : in integer) return node_ptr is
            variable n : node_ptr := new node_t'(val => v, next_node => head);
        begin
            return n;
        end function;

        procedure unlink (variable p : inout node_ptr) is
            variable old : node_ptr := p;
        begin
            p := p.next_node;
            deallocate(old);
        end procedure;

        procedure push (constant v : in integer) is
        begin
            head := alloc_node(v);
        end procedure;

        impure function pop return integer is
            variable res : integer;
        begin
            res := head.val;
            unlink(head);
            return res;
        end function;
    end protected body;

    shared variable b : box_t;
begin
    process is
    begin
        b.push(10);
        b.push(20);
        assert b.pop = 20;
        assert b.pop = 10;
        wait;
    end process;
end architecture;
