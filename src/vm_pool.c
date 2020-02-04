/*
 * vm_pool.c - Unlambda VM object pool
 * 
 * Copyright (C) 2020  CismonX <admin@cismon.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "vm_pool.h"
#include "vm_stack.h"

#include <stddef.h>
#include <stdlib.h>

struct vm_pool_elem {
    struct u6a_vm_var_tuple values;
    uint32_t                refcnt;
    uint32_t                flags;
};

#define POOL_ELEM_HOLDS_PTR ( 1 << 0 )

struct vm_pool {
    uint32_t pos;
    struct vm_pool_elem elems[];
};

struct vm_pool_elem_ptrs {
    uint32_t pos;
    struct vm_pool_elem* elems[];
};

static struct vm_pool*           active_pool;
static struct vm_pool_elem_ptrs* holes;
static        uint32_t           pool_len;
static struct vm_pool_elem**     fstack;
static        uint32_t           fstack_len;
static        uint32_t           fstack_top;

static inline struct vm_pool_elem*
vm_pool_elem_alloc() {
    struct vm_pool_elem* new_elem;
    if (holes->pos == UINT32_MAX) {
        if (UNLIKELY(++active_pool->pos >= pool_len)) {
            return NULL;
        }
        new_elem = active_pool->elems + active_pool->pos;
    } else {
        new_elem = holes->elems[holes->pos--];
    }
    new_elem->refcnt = 1;
    return new_elem;
}

static inline struct vm_pool_elem*
vm_pool_elem_dup(struct vm_pool_elem* elem) {
    struct vm_pool_elem* new_elem = vm_pool_elem_alloc();
    if (UNLIKELY(new_elem == NULL)) {
        return NULL;
    }
    *new_elem = *elem;
    return new_elem;
}

static inline void
free_stack_push(union u6a_vm_var var) {
    if (var.fn.token.fn & U6A_VM_FN_REF) {
        fstack[++fstack_top] = active_pool->elems + var.fn.ref;
    }
}

static inline struct vm_pool_elem*
free_stack_pop() {
    if (fstack_top == UINT32_MAX) {
        return NULL;
    }
    return fstack[fstack_top--];
}

bool
u6a_vm_pool_init(uint32_t pool_len_, uint32_t ins_len) {
    const uint32_t pool_size = sizeof(struct vm_pool) + pool_len_ * sizeof(struct vm_pool_elem);
    active_pool = malloc(pool_size);
    if (UNLIKELY(active_pool == NULL)) {
        return false;
    }
    const uint32_t holes_size = sizeof(struct vm_pool_elem_ptrs) + pool_len_ * sizeof(struct vm_pool_elem*);
    holes = malloc(holes_size);
    if (UNLIKELY(holes == NULL)) {
        free(holes);
        return false;
    }
    const uint32_t free_stack_size = ins_len * sizeof(struct vm_pool_elem*);
    fstack = malloc(free_stack_size);
    if (UNLIKELY(fstack == NULL)) {
        free(active_pool);
        free(holes);
        return false;
    }
    active_pool->pos = UINT32_MAX;
    holes->pos = UINT32_MAX;
    pool_len = pool_len_;
    fstack_len = ins_len;
    return true;
}

U6A_HOT uint32_t
u6a_vm_pool_alloc1(struct u6a_vm_var_fn v1) {
    struct vm_pool_elem* elem = vm_pool_elem_alloc();
    if (UNLIKELY(elem == NULL)) {
        return UINT32_MAX;
    }
    elem->values.v1.fn = v1;
    elem->flags = 0;
    return elem - active_pool->elems;
}

U6A_HOT uint32_t
u6a_vm_pool_fill2(uint32_t offset, struct u6a_vm_var_fn v2) {
    struct vm_pool_elem* elem = active_pool->elems + offset;
    if (elem->refcnt == 1) {
        elem->values.v2.fn = v2;
        elem->flags = 0;
        return offset;
    } else {
        elem = vm_pool_elem_dup(elem);
        if (UNLIKELY(elem == NULL)) {
            return UINT32_MAX;
        }
        elem->values.v2.fn = v2;
        elem->flags = 0;
        return elem - active_pool->elems;
    }
}

U6A_HOT uint32_t
u6a_vm_pool_alloc2(struct u6a_vm_var_fn v1, struct u6a_vm_var_fn v2) {
    struct vm_pool_elem* elem = vm_pool_elem_alloc();
    if (UNLIKELY(elem == NULL)) {
        return UINT32_MAX;
    }
    elem->values = (struct u6a_vm_var_tuple) { .v1.fn = v1, .v2.fn = v2 };
    elem->flags = 0;
    return elem - active_pool->elems;
}

U6A_HOT uint32_t
u6a_vm_pool_alloc2_ptr(void* v1, void* v2) {
    struct vm_pool_elem* elem = vm_pool_elem_alloc();
    if (UNLIKELY(elem == NULL)) {
        return UINT32_MAX;
    }
    elem->values = (struct u6a_vm_var_tuple) { .v1.ptr = v1, .v2.ptr = v2 };
    elem->flags = POOL_ELEM_HOLDS_PTR;
    return elem - active_pool->elems;
}

U6A_HOT union u6a_vm_var
u6a_vm_pool_get1(uint32_t offset) {
    return (active_pool->elems + offset)->values.v1;
}

U6A_HOT struct u6a_vm_var_tuple
u6a_vm_pool_get2(uint32_t offset) {
    return (active_pool->elems + offset)->values;
}

U6A_HOT struct u6a_vm_var_tuple
u6a_vm_pool_get2_separate(uint32_t offset) {
    struct vm_pool_elem* elem = active_pool->elems + offset;
    struct u6a_vm_var_tuple values = elem->values;
    if (elem->refcnt > 1) {
        // Continuation having more than 1 reference should be separated before reinstatement
        values.v1.ptr = u6a_vm_stack_dup(values.v1.ptr);
    }
    return values;
}

U6A_HOT void
u6a_vm_pool_addref(uint32_t offset) {
    ++active_pool->elems[offset].refcnt;
}

U6A_HOT void
u6a_vm_pool_free(uint32_t offset) {
    struct vm_pool_elem* elem = active_pool->elems;
    fstack_top = UINT32_MAX;
    do {
        if (--elem->refcnt == 0) {
            holes->elems[++holes->pos] = elem;
            if (elem->flags & POOL_ELEM_HOLDS_PTR) {
                // Continuation destroyed before used
                u6a_vm_stack_discard(elem->values.v1.ptr);
            } else {
                free_stack_push(elem->values.v1);
                free_stack_push(elem->values.v2);
            }
        }
    } while ((elem = free_stack_pop()));
}

void
u6a_vm_pool_destroy() {
    free(active_pool);
    free(holes);
    free(fstack);
}
