//
//  Copyright (C) 2022-2026  Nick Gasson
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "test_util.h"
#include "jit/jit.h"
#include "option.h"
#include "rt/mspace.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

START_TEST(test_sanity)
{
   mspace_t *m = mspace_new(1024, NULL);

   int *ptr1 = mspace_alloc(m, sizeof(int));
   ck_assert_ptr_nonnull(ptr1);
   *ptr1 = 42;

   int *ptr2 = mspace_alloc(m, sizeof(int));
   ck_assert_ptr_nonnull(ptr2);
   ck_assert_ptr_ne(ptr1, ptr2);
   *ptr2 = 55;

   ck_assert_ptr_null(mspace_alloc(m, 0));

   int *array = mspace_alloc(m, 50 * sizeof(int));
   array[49] = 1234;
   array = NULL;

   // Do enough allocations to trigger a GC
   for (int i = 0; i < 1000; i++) {
      int *garbage = mspace_alloc(m, sizeof(int));
      ck_assert_ptr_nonnull(garbage);
      ck_assert_ptr_ne(garbage, ptr1);
      ck_assert_ptr_ne(garbage, ptr2);
      *garbage = 0xdead;
   }

   ck_assert_int_eq(*ptr1, 42);
   ck_assert_int_eq(*ptr2, 55);

   mspace_destroy(m);
}
END_TEST

__attribute__((noinline))
static int **get_indirect(mspace_t *m)
{
   int *ptr1 = mspace_alloc(m, sizeof(int));
   ck_assert_ptr_nonnull(ptr1);
   *ptr1 = 42;

   int **ptr2 = mspace_alloc(m, sizeof(int *));
   ck_assert_ptr_nonnull(ptr2);
   ck_assert_ptr_ne(ptr1, ptr2);
   *ptr2 = ptr1;

   return ptr2;
}

__attribute__((noinline))
static void generate_garbage(mspace_t *m, int count, size_t size)
{
   for (int i = 0; i < count; i++) {
      int *garbage = mspace_alloc(m, size);
      ck_assert_ptr_nonnull(garbage);
      *garbage = 0xdead;
   }
}

START_TEST(test_indirect)
{
   mspace_t *m = mspace_new(1024, NULL);

   // Do a few dummy allocations to ensure the pointer is not in the
   // first line which is often kept alive by stack pointers to m->space
   generate_garbage(m, 5, sizeof(int));

   int *volatile* ptr2 = get_indirect(m);

   // Do enough allocations to trigger a GC
   generate_garbage(m, 1000, sizeof(int));

   ck_assert_int_eq(**ptr2, 42);

   mspace_destroy(m);
}
END_TEST

START_TEST(test_mptr)
{
   mspace_t *m = mspace_new(1024, NULL);

   // Do a few dummy allocations to ensure the pointer is not in the
   // first line which is often kept alive by stack pointers to m->space
   generate_garbage(m, 5, sizeof(int));

   mptr_t p = mptr_new(m, "test");
   *mptr_get(p) = mspace_alloc(m, sizeof(int));
   *(int *)*mptr_get(p) = 42;

   // Do enough allocations to trigger a GC
   generate_garbage(m, 1000, sizeof(int));

   ck_assert_int_eq(*(int *)*mptr_get(p), 42);

   mptr_free(m, &p);
   ck_assert_ptr_eq(p, MPTR_INVALID);

   mspace_destroy(m);
}
END_TEST

START_TEST(test_large)
{
   mspace_t *m = mspace_new(5 * 1024, NULL);

   // Do a few dummy allocations to ensure the pointer is not in the
   // first line which is often kept alive by stack pointers to m->space
   generate_garbage(m, 50, sizeof(int));

   int *volatile* parray = get_indirect(m);

   *parray = mspace_alloc(m, 100 * sizeof(int));
   (*parray)[99] = 1234;

   // Do enough allocations to trigger a GC
   generate_garbage(m, 1000, 5 * sizeof(int));

   ck_assert_int_eq((*parray)[99], 1234);

   mspace_destroy(m);
}
END_TEST

START_TEST(test_interior)
{
   mspace_t *m = mspace_new(5 * 1024, NULL);

   // Do a few dummy allocations to ensure the pointer is not in the
   // first line which is often kept alive by stack pointers to m->space
   generate_garbage(m, 10, sizeof(int));

   int *volatile* parray = get_indirect(m);

   // Interior pointers keep the whole allocation alive
   *parray = mspace_alloc(m, 100 * sizeof(int)) + (13 * sizeof(int));
   (*parray)[49] = 1234;

   // Do enough allocations to trigger a GC
   generate_garbage(m, 1000, 5 * sizeof(int));

   ck_assert_int_eq((*parray)[49], 1234);

   mspace_destroy(m);
}
END_TEST

static size_t oom_sz = 0;

static void test_oom_cb(mspace_t *m, size_t size, void *ctx)
{
   oom_sz = size;
   ck_assert_ptr_eq(ctx, (void *)0xdeadbeef);
}

START_TEST(test_oom)
{
   const mspace_handler_t handler = {
      .oom = test_oom_cb,
      .context = (void *)0xdeadbeef,
   };

   mspace_t *m = mspace_new(1024, &handler);

   void *p = mspace_alloc(m, 1000000);
   ck_assert_ptr_null(p);
   ck_assert_int_eq(oom_sz, 1000000);

   mspace_destroy(m);
}
END_TEST

START_TEST(test_linked_list)
{
   struct list {
      struct list *next;
      int value;
   };

   mspace_t *m = mspace_new(4096, NULL);

   struct list *head = NULL, **tailp = &head;

   for (int i = 0; i < 40; i++) {
      struct list *l = mspace_alloc(m, sizeof(struct list));
      l->value = i;
      l->next = NULL;

      *tailp = l;
      tailp = &(l->next);

      // Do some dummy allocations to make sure the nodes are at random
      // locations in the heap
      for (int i = 0; i < 10; i++) {
         int *garbage = mspace_alloc(m, (1 + rand() % 10) * sizeof(int));
         ck_assert_ptr_nonnull(garbage);
         *garbage = 0xdead;
      }
   }

   struct list *it = head;
   for (int i = 0; i < 40; i++, it = it->next)
      ck_assert_int_eq(it->value, i);

   mspace_destroy(m);
}
END_TEST

__attribute__((noinline))
static void put_value(mspace_t *m, int **ptr, int value)
{
   int *st = mspace_alloc(m, sizeof(int));
   *st = value;
   *ptr = st;
}

START_TEST(test_tlab)
{
   opt_set_size(OPT_HEAP_SIZE, 2 * TLAB_SIZE);

   jit_t *j = jit_new(NULL, NULL);

   tlab_t *t = tlab_acquire(j);

   int *precious = tlab_alloc(t, sizeof(int));
   *precious = 0xbeef;

   int **array = tlab_alloc(t, sizeof(int *) * 10);
   for (int i = 0; i < 10; i++)
      put_value(jit_get_mspace(j), array + i, i);

   int **p1 = tlab_alloc(t, sizeof(int *));
   *p1 = precious;

   for (int i = 0; i < 1000; i++) {
      int *array = tlab_alloc(t, 50 * sizeof(int));
      array[0] = 123;

      for (int i = 0; i < 10; i++) {
         int *garbage = mspace_alloc(jit_get_mspace(j), sizeof(int));
         ck_assert_ptr_nonnull(garbage);
         *garbage = 0xdead;
      }
   }

   ck_assert_int_eq(*precious, 0xbeef);

   for (int i = 0; i < 10; i++)
      ck_assert_int_eq(*array[i], i);

   tlab_release(j, t);
   jit_free(j);
}
END_TEST

START_TEST(test_end_ptr)
{
   mspace_t *m = mspace_new(5 * 1024, NULL);

   // Do a few dummy allocations to ensure the pointer is not in the
   // first line which is often kept alive by stack pointers to m->space
   generate_garbage(m, 50, sizeof(int));

   // Size is a multiple of LINE_SIZE
   static const int SIZE = 64;
   int *volatile array = mspace_alloc(m, SIZE * sizeof(int));
   for (int i = 0; i < SIZE; i++)
      *array++ = i;
   // Now array points one past the end of the of the allocation

   // Do enough allocations to trigger a GC
   generate_garbage(m, 1000, 5 * sizeof(int));

   ck_assert_int_eq(array[-SIZE], 0);
   ck_assert_int_eq(array[-1], SIZE - 1);

   mspace_destroy(m);
}
END_TEST

static void user_mark_cb(mspace_t *m, void *cookie, void *ctx)
{
   mspace_user_mark(m, cookie, ctx, sizeof(int *));
}

START_TEST(test_user_mark)
{
   int **precious = malloc(sizeof(int *));

   const mspace_handler_t handler = {
      .mark = user_mark_cb,
      .context = precious,
   };

   mspace_t *m = mspace_new(1024, &handler);

   (void)mspace_alloc(m, 500);

   *precious = mspace_alloc(m, sizeof(int));
   **precious = 0xbeef;

   for (int i = 0; i < 10000; i++) {
      int *garbage = mspace_alloc(m, sizeof(int));
      ck_assert_ptr_nonnull(garbage);
      *garbage = 0xdead;
   }

   ck_assert_int_eq(**precious, 0xbeef);

   free(precious);
   mspace_destroy(m);
}
END_TEST

static int cmp_ptr(const void *a, const void *b)
{
   const uintptr_t x = **(const uintptr_t **)a, y = **(const uintptr_t **)b;
   return x < y ? -1 : x > y;
}

START_TEST(test_free_list_stress)
{
   // Mixed small and large allocations with many dead objects between the
   // live ones so the free list has many fragments after each collection:
   // live objects must not overlap or be corrupted and the allocation
   // hints must always agree with a search of the whole free list

   enum { NSLOTS = 1000, HEAP = 4 * 1024 * 1024 };

   mspace_t *m = mspace_new(HEAP, NULL);
   ck_assert(mspace_check_free_list(m));

   mptr_t root = mptr_new(m, "slots");
   uint8_t **slots = mspace_alloc(m, NSLOTS * sizeof(uint8_t *));
   *mptr_get(root) = slots;

   size_t *sizes = calloc(NSLOTS, sizeof(size_t));

   srand(1234);
   for (int iter = 0; iter < 4000; iter++) {
      const int slot = rand() % NSLOTS;
      const int kind = rand() % 100;
      size_t size;
      if (kind < 70)
         size = 1 + rand() % 400;
      else if (kind < 98)
         size = 400 + rand() % 3700;
      else
         size = 4096 + rand() % 30000;

      // Garbage which dies immediately
      if (rand() % 3 == 0)
         ck_assert_ptr_nonnull(mspace_alloc(m, 1 + rand() % 2000));

      uint8_t *p = mspace_alloc(m, size);
      ck_assert_ptr_nonnull(p);
      memset(p, slot & 0xff, size);
      slots[slot] = p;
      sizes[slot] = size;

      if (iter % 1000 == 999) {
         ck_assert(mspace_check_free_list(m));

         uint8_t **order[NSLOTS];
         int n = 0;
         for (int i = 0; i < NSLOTS; i++) {
            if (slots[i] == NULL)
               continue;

            for (size_t j = 0; j < sizes[i]; j++)
               ck_assert_int_eq(slots[i][j], i & 0xff);

            order[n++] = &(slots[i]);
         }

         qsort(order, n, sizeof(uint8_t **), cmp_ptr);
         for (int i = 1; i < n; i++) {
            const uint8_t *prev = *order[i - 1], *next = *order[i];
            ck_assert(prev + sizes[order[i - 1] - slots] <= next);
         }
      }
   }

   ck_assert(mspace_check_free_list(m));

   free(sizes);
   mptr_free(m, &root);
   mspace_destroy(m);
}
END_TEST

Suite *get_mspace_tests(void)
{
   Suite *s = suite_create("mspace");

   TCase *tc = nvc_unit_test();
   tcase_add_test(tc, test_sanity);
   tcase_add_test(tc, test_indirect);
   tcase_add_test(tc, test_mptr);
   tcase_add_test(tc, test_large);
   tcase_add_test(tc, test_interior);
   tcase_add_test(tc, test_oom);
   tcase_add_test(tc, test_linked_list);
   tcase_add_test(tc, test_tlab);
   tcase_add_test(tc, test_end_ptr);
   tcase_add_test(tc, test_user_mark);
   tcase_add_test(tc, test_free_list_stress);
   suite_add_tcase(s, tc);

   return s;
}
