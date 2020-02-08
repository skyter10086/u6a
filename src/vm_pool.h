/*
 * vm_pool.h - Unlambda VM object pool definitions
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

#ifndef U6A_VM_POOL_H_
#define U6A_VM_POOL_H_

#include "common.h"
#include "vm_defs.h"

#include <stdint.h>
#include <stdbool.h>

bool
u6a_vm_pool_init(uint32_t pool_len, uint32_t ins_len, const char* err_stage);

uint32_t
u6a_vm_pool_alloc1(struct u6a_vm_var_fn v1);

uint32_t
u6a_vm_pool_alloc2(struct u6a_vm_var_fn v1, struct u6a_vm_var_fn v2);

uint32_t
u6a_vm_pool_alloc2_ptr(void* v1, void* v2);

union u6a_vm_var
u6a_vm_pool_get1(uint32_t offset);

struct u6a_vm_var_tuple
u6a_vm_pool_get2(uint32_t offset);

struct u6a_vm_var_tuple
u6a_vm_pool_get2_separate(uint32_t offset);

void
u6a_vm_pool_addref(uint32_t offset);

void
u6a_vm_pool_free(uint32_t offset);

void
u6a_vm_pool_destroy();

#endif
