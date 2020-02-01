/*
 * runtime.c - Unlambda runtime
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

#include "runtime.h"
#include "logging.h"
#include "vm_defs.h"
#include "vm_stack.h"
#include "vm_pool.h"

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

static struct u6a_vm_ins* text;
static        uint32_t    text_len;
static        char*       rodata;
static        uint32_t    rodata_len;
static        bool        force_exec;

static const struct u6a_vm_ins text_subst[] = {
    { .opcode = u6a_vo_la  },
    { .opcode = u6a_vo_xch },
    { .opcode = u6a_vo_la  },
    { .opcode = u6a_vo_la  },
    { .opcode = u6a_vo_la  }
};
static const uint32_t text_subst_len = sizeof(text_subst) / sizeof(struct u6a_vm_ins);

static const char* err_runtime = "runtime error";
static const char* info_runtime = "runtime";

#define CHECK_BC_HEADER_VER(file_header)                     \
    ( (file_header).ver_major == U6A_VER_MAJOR && (file_header).ver_minor == U6A_VER_MINOR )

#define ACC_FN_REF(fn_, ref_)                                \
    acc = U6A_VM_VAR_FN_REF(fn_, ref_);                      \
    if (UNLIKELY(acc.ref == UINT32_MAX)) {                   \
        goto runtime_error;                                  \
    }
#define CHECK_FORCE()                                        \
    if (!force_exec) {                                       \
        goto runtime_error;                                  \
    }

#define STACK_PUSH1(fn_0)                                    \
    if (UNLIKELY(!u6a_vm_stack_push1(fn_0))) {               \
        goto runtime_error;                                  \
    }
#define STACK_PUSH2(fn_0, fn_1)                              \
    if (UNLIKELY(!u6a_vm_stack_push2(fn_0, fn_1))) {         \
        goto runtime_error;                                  \
    }
#define STACK_PUSH3(fn_0, fn_12)                             \
    if (UNLIKELY(!u6a_vm_stack_push3(fn_0, fn_12))) {        \
        goto runtime_error;                                  \
    }
#define STACK_PUSH4(fn_0, fn_1, fn_23)                       \
    if (UNLIKELY(!u6a_vm_stack_push4(fn_0, fn_1, fn_23))) {  \
        goto runtime_error;                                  \
    }
#define STACK_POP()                                          \
    u6a_vm_stack_top();                                      \
    if (UNLIKELY(!u6a_vm_stack_pop())) {                     \
        goto runtime_error;                                  \
    }

#define POOL_GET1(offset)                                    \
    u6a_vm_pool_get1(offset).fn;                             \
    u6a_vm_pool_free(offset)
#define POOL_GET2(offset)                                    \
    u6a_vm_pool_get2(offset);                                \
    u6a_vm_pool_free(offset)

static inline bool
read_bc_header(struct u6a_bc_header* restrict header, FILE* restrict input_stream) {
    int ch;
    do {
        ch = fgetc(input_stream);
        if (UNLIKELY(ch == EOF)) {
            return false;
        }
    } while (ch != U6A_MAGIC);
    if (UNLIKELY(ch != ungetc(ch, input_stream))) {
        return false;
    }
    if (UNLIKELY(1 != fread(&header->file, U6A_BC_FILE_HEADER_SIZE, 1, input_stream))) {
        return false;
    }
    if (LIKELY(header->file.prog_header_size >= U6A_BC_FILE_HEADER_SIZE)) {
        return 1 == fread(&header->prog, header->file.prog_header_size, 1, input_stream);
    }
    return true;
}

static inline void
vm_var_fn_free(struct u6a_vm_var_fn var) {
    if (var.token.fn & U6A_VM_FN_REF) {
        u6a_vm_pool_free(var.ref);
    }
}

bool
u6a_runtime_info(FILE* restrict input_stream, const char* file_name) {
    struct u6a_bc_header header;
    if (UNLIKELY(!read_bc_header(&header, input_stream))) {
        u6a_err_invalid_bc_file(err_runtime, file_name);
        return false;
    }
    printf("Version: %d.%d.X\n", header.file.ver_major, header.file.ver_minor);
    if (LIKELY(CHECK_BC_HEADER_VER(header.file))) {
        if (LIKELY(header.file.prog_header_size == U6A_BC_FILE_HEADER_SIZE)) {
            printf("Size of section .text   (bytes): 0x%08X\n", ntohl(header.prog.text_size));
            printf("Size of section .rodata (bytes): 0x%08X\n", ntohl(header.prog.rodata_size));
        } else {
            printf("Program header unrecognizable (%d bytes)", header.file.prog_header_size);
        }
    }
    return true;
}

bool
u6a_runtime_init(struct u6a_runtime_options* options) {
    struct u6a_bc_header header;
    if (UNLIKELY(!read_bc_header(&header, options->istream))) {
        u6a_err_invalid_bc_file(err_runtime, options->file_name);
        return false;
    }
    if (UNLIKELY(!CHECK_BC_HEADER_VER(header.file))) {
        if (!options->force_exec || header.file.prog_header_size != U6A_BC_FILE_HEADER_SIZE) {
            u6a_err_bad_bc_ver(err_runtime, options->file_name, header.file.ver_major, header.file.ver_minor);
            return false;
        }
    }
    header.prog.text_size = ntohl(header.prog.text_size);
    header.prog.rodata_size = ntohl(header.prog.rodata_size);
    text = malloc(header.prog.text_size + sizeof(text_subst));
    if (UNLIKELY(text == NULL)) {
        u6a_err_bad_alloc(err_runtime, header.prog.text_size + sizeof(text_subst));
        return false;
    }
    text_len = header.prog.text_size / sizeof(struct u6a_vm_ins);
    rodata = malloc(header.prog.rodata_size);
    if (UNLIKELY(rodata == NULL)) {
        u6a_err_bad_alloc(err_runtime, header.prog.rodata_size);
        free(text);
        return false;
    }
    rodata_len = header.prog.rodata_size / sizeof(char);
    memcpy(text, text_subst, sizeof(text_subst));
    if (UNLIKELY(text_len != fread(text + text_subst_len, sizeof(struct u6a_vm_ins), text_len, options->istream))) {
        goto runtime_init_failed;
    }
    if (UNLIKELY(rodata_len != fread(rodata, sizeof(char), rodata_len, options->istream))) {
        goto runtime_init_failed;
    }
    if (UNLIKELY(!u6a_vm_stack_init(options->stack_segment_size))) {
        goto runtime_init_failed;
    }
    if (UNLIKELY(!u6a_vm_pool_init(options->pool_size, text_len))) {
        goto runtime_init_failed;
    }
    for (struct u6a_vm_ins* ins = text + text_subst_len; ins < text + text_len; ++ins) {
        if (ins->opcode & U6A_VM_OP_OFFSET) {
            ins->operand.offset = ntohl(ins->operand.offset);
        }
    }
    force_exec = options->force_exec;
    return true;

    runtime_init_failed:
    u6a_runtime_destroy();
    return false;
}

U6A_HOT union u6a_vm_var
u6a_runtime_execute(FILE* restrict istream, FILE* restrict ostream) {
    struct u6a_vm_var_fn acc;
    struct u6a_vm_ins* ins = text + text_subst_len;
    int current_char = EOF;
    struct u6a_vm_var_fn func, arg;
    struct u6a_vm_var_tuple tuple;
    void* ptr;
    while (true) {
        switch (ins->opcode) {
            case u6a_vo_app:
                if (ins->operand.fn.first.fn) {
                    func.token = ins->operand.fn.first;
                } else {
                    func = acc;
                }
                if (ins->operand.fn.second.fn) {
                    arg.token = ins->operand.fn.second;
                } else {
                    arg = acc;
                }
                goto do_apply;
            case u6a_vo_la:
                func = STACK_POP();
                arg = acc;
                do_apply:
                switch (func.token.fn) {
                    case u6a_vf_s:
                        ACC_FN_REF(u6a_vf_s1, u6a_vm_pool_alloc1(arg));
                        break;
                    case u6a_vf_s1:
                        ACC_FN_REF(u6a_vf_s2, u6a_vm_pool_fill2(func.ref, arg));
                        break;
                        ACC_FN_REF(u6a_vf_d1_c, u6a_vm_pool_alloc1(arg));
                    case u6a_vf_s2:
                        if (ins++ - text == 0x03) {
                            STACK_PUSH3(arg, u6a_vm_pool_get2(func.ref));
                        } else {
                            STACK_PUSH4(U6A_VM_VAR_FN_REF(u6a_vf_j, ins - text), arg, u6a_vm_pool_get2(func.ref));
                        }
                        u6a_vm_pool_free(func.ref);
                        ins = text;
                        goto do_apply;
                    case u6a_vf_k:
                        ACC_FN_REF(u6a_vf_k1, u6a_vm_pool_alloc1(arg));
                        break;
                    case u6a_vf_k1:
                        vm_var_fn_free(arg);
                        acc = POOL_GET1(func.ref);
                        break;
                    case u6a_vf_i:
                        acc = arg;
                        break;
                    case u6a_vf_out:
                        acc = arg;
                        // May fail to print, but we don't care
                        fputc(func.token.ch, ostream);
                        break;
                    case u6a_vf_j:
                        acc = arg;
                        ins = text + func.ref;
                        continue;
                    case u6a_vf_f:
                        // Safe to assign IP here before jumping, as func won't be `j` or `f`
                        ins = text + func.ref;
                        func = acc;
                        // The `s2` function incremenets IP before pushing `j`, so decrement IP here
                        if (func.token.fn = u6a_vf_s2) {
                            --ins;
                        }
                        arg = STACK_POP();
                        goto do_apply;
                    case u6a_vf_c:
                        ptr = u6a_vm_stack_save();
                        if (UNLIKELY(ptr == NULL)) {
                            goto runtime_error;
                        }
                        ACC_FN_REF(u6a_vf_c1, u6a_vm_pool_alloc2_ptr(ptr, ins));
                        break;
                    case u6a_vf_d:
                        ACC_FN_REF(u6a_vf_d1_c, u6a_vm_pool_alloc1(arg));
                        break;
                    case u6a_vf_c1:
                        tuple = POOL_GET2(func.ref);
                        u6a_vm_stack_resume(tuple.v1.ptr);
                        ins = tuple.v2.ptr;
                        acc = arg;
                        break;
                    case u6a_vf_d1_c:
                        func = POOL_GET1(func.ref);
                        goto do_apply;
                    case u6a_vf_d1_s: ;
                        tuple = POOL_GET2(func.ref);
                        STACK_PUSH1(tuple.v1.fn);
                        acc = tuple.v2.fn;
                        ins = text + 0x03;
                        continue;
                    case u6a_vf_d1_d:
                        STACK_PUSH2(arg, U6A_VM_VAR_FN_REF(u6a_vf_f, ins - text));
                        ins = text + func.ref;
                        continue;
                    case u6a_vf_v:
                        vm_var_fn_free(arg);
                        acc.token.fn = u6a_vf_v;
                        break;
                    case u6a_vf_p:
                        acc = arg;
                        fputs(rodata + func.ref, ostream);
                        break;
                    case u6a_vf_in:
                        current_char = fgetc(istream);
                        func = arg;
                        arg.token.fn = current_char == EOF ? u6a_vf_v : u6a_vf_i;
                        goto do_apply;
                    case u6a_vf_cmp:
                        acc = func;
                        func = arg;
                        arg.token.fn = acc.token.ch == current_char ? u6a_vf_i : u6a_vf_v;
                        goto do_apply;
                    case u6a_vf_pipe:
                        func = arg;
                        if (UNLIKELY(current_char == EOF)) {
                            arg.token.fn = u6a_vf_v;acc = u6a_vm_stack_xch(acc);
                        } else {
                            arg.token = U6A_TOKEN(u6a_vf_out, current_char);
                        }
                        goto do_apply;
                    case u6a_vf_e:
                        // Every program should terminate with explicit `e` function
                        return U6A_VM_VAR_FN(arg);
                    default:
                        CHECK_FORCE();
                }
                break;
            case u6a_vo_sa:
                if (acc.token.fn == u6a_vf_d) {
                    goto delay;
                }
                STACK_PUSH1(acc);
                break;
            case u6a_vo_xch:
                if (acc.token.fn == u6a_vf_d) {
                    func = STACK_POP();
                    arg = STACK_POP();
                    ACC_FN_REF(u6a_vf_d1_s, u6a_vm_pool_alloc2(func, arg));
                } else {
                    acc = u6a_vm_stack_xch(acc);
                }
                break;
            case u6a_vo_del:
                delay:
                acc = U6A_VM_VAR_FN_REF(u6a_vf_d1_d, ins + 1 - text);
                ins = text + ins->operand.offset;
                continue;
            case u6a_vo_lc:
                switch (ins->opcode_ex) {
                    case u6a_vo_ex_print:
                        acc = U6A_VM_VAR_FN_REF(u6a_vf_p, ins->operand.offset);
                        break;
                    default:
                        CHECK_FORCE();
                }
                break;
            default:
                CHECK_FORCE();
        }
        ++ins;
    }

    runtime_error:
    return U6A_VM_VAR_PTR(NULL);
}

void
u6a_runtime_destroy() {
    free(text);
    free(rodata);
    text = NULL;
    rodata = NULL;
}
