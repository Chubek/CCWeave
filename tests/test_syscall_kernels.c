#include "GlueSTD.h"
#include "ccw_host_accessors.h"
#include "ccw_ir.h"
#include "ccw_test.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef CCW_KERNEL_DIR
#define CCW_KERNEL_DIR "kernels"
#endif

static ccw_ir *
sample(const char *opcode, ccw_profile profile, ccw_node *block_out)
{
  ccw_ir *ir = ccw_ir_module_create("syscall", profile);
  ccw_node fn = ccw_ir_function_add(ir, "main", CCW_TY_I64);
  ccw_node block = ccw_ir_block_add(ir, fn, "entry");
  ccw_node ins = ccw_ir_instr_build(ir, opcode, CCW_TY_I64);
  ccw_ir_instr_set_dest(ir, ins, "result");
  ccw_ir_instr_add_operand(
      ir, ins, ccw_ir_operand_const_int(ir, CCW_TY_I64, 1));
  ccw_ir_instr_add_operand(ir, ins, ccw_ir_operand_const_int(ir, CCW_TY_I64, 1));
  ccw_ir_block_append_instr(ir, block, ins);
  *block_out = block;
  return ir;
}

static void
apply_one(ccw_executor *ex, const char *file, const char *cap,
          const char *expected, ccw_profile profile)
{
  char path[512];
  char *error = NULL;
  ccw_node block = 0;
  ccw_ir *ir = sample("syscall", profile, &block);
  snprintf(path, sizeof(path), "%s/%s", CCW_KERNEL_DIR, file);
  int id = ccw_kernel_load(ex, path, &error);
  CCW_CHECK(id >= 0, "%s failed to load: %s", file, error ? error : "");
  free(error);
  if (id >= 0)
    {
      error = NULL;
      CCW_CHECK(ccw_kernel_apply(ex, id, cap, ir, NULL, &error) == CCW_OK,
                "%s failed to apply: %s", file, error ? error : "");
      free(error);
      CCW_CHECK_STREQ(
          ccw_ir_instr_opcode(ir, ccw_ir_block_instr_ref(ir, block, 0)),
          expected);
    }
  ccw_ir_module_destroy(ir);
}

static int
load_apply(ccw_executor *ex, const char *file, const char *cap, ccw_ir *ir)
{
  char path[512];
  char *error = NULL;
  int id;
  snprintf(path, sizeof(path), "%s/%s", CCW_KERNEL_DIR, file);
  id = ccw_kernel_load(ex, path, &error);
  CCW_CHECK(id >= 0, "%s failed to load: %s", file, error ? error : "");
  free(error);
  if (id < 0)
    return 0;
  error = NULL;
  CCW_CHECK(ccw_kernel_apply(ex, id, cap, ir, NULL, &error) == CCW_OK,
            "%s failed to apply: %s", file, error ? error : "");
  free(error);
  return 1;
}

static void
test_io_open(ccw_executor *ex, const char *io_file, const char *io_cap,
             int64_t expected_number, int expected_operands,
             const char *syscall_file, const char *syscall_cap,
             const char *expected_trap)
{
  ccw_ir *ir = ccw_ir_module_create("io", CCW_PROFILE_TILLY);
  ccw_node fn = ccw_ir_function_add(ir, "main", CCW_TY_I64);
  ccw_node block = ccw_ir_block_add(ir, fn, "entry");
  ccw_node path = ccw_ir_operand_reg(ir, "path");
  ccw_node flags = ccw_ir_operand_const_int(ir, CCW_TY_I64, 0);
  ccw_node mode = ccw_ir_operand_const_int(ir, CCW_TY_I64, 0);
  ccw_node open = ccw_ir_build_io_open(ir, block, "fd", path, flags, mode);
  CCW_CHECK(open != 0, "I/O C builder failed");
  if (load_apply(ex, io_file, io_cap, ir))
    {
      ccw_node lowered = ccw_ir_block_instr_ref(ir, block, 0);
      int64_t number = -1;
      CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, lowered), "syscall");
      CCW_CHECK(ccw_ir_instr_operand_count(ir, lowered) == expected_operands,
                "unexpected syscall operand count");
      CCW_CHECK(ccw_ir_const_int_value(
                    ir, ccw_ir_instr_operand(ir, lowered, 0), &number)
                    == CCW_OK
                    && number == expected_number,
                "unexpected open syscall number");
      if (load_apply(ex, syscall_file, syscall_cap, ir))
        CCW_CHECK_STREQ(
            ccw_ir_instr_opcode(ir, ccw_ir_block_instr_ref(ir, block, 0)),
            expected_trap);
    }
  ccw_ir_module_destroy(ir);
}

static void
test_io_wrapper(ccw_executor *ex, const char *io_file, const char *io_cap,
                const char *operation, int64_t expected_number)
{
  ccw_ir *ir = ccw_ir_module_create("io-wrapper", CCW_PROFILE_ON1X);
  ccw_node fn = ccw_ir_function_add(ir, "main", CCW_TY_I64);
  ccw_node block = ccw_ir_block_add(ir, fn, "entry");
  ccw_node fd = ccw_ir_operand_const_int(ir, CCW_TY_I64, 1);
  ccw_node buffer = ccw_ir_operand_reg(ir, "buffer");
  ccw_node count = ccw_ir_operand_const_int(ir, CCW_TY_I64, 4);
  ccw_node instruction;
  int64_t number = -1;
  if (strcmp(operation, "read") == 0)
    instruction = ccw_ir_build_io_read(ir, block, "result", fd, buffer, count);
  else if (strcmp(operation, "write") == 0)
    instruction = ccw_ir_build_io_write(ir, block, "result", fd, buffer, count);
  else
    instruction = ccw_ir_build_io_close(ir, block, "result", fd);
  CCW_CHECK(instruction != 0, "I/O %s C builder failed", operation);
  if (load_apply(ex, io_file, io_cap, ir))
    {
      ccw_node lowered = ccw_ir_block_instr_ref(ir, block, 0);
      CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, lowered), "syscall");
      CCW_CHECK(ccw_ir_const_int_value(
                    ir, ccw_ir_instr_operand(ir, lowered, 0), &number)
                    == CCW_OK
                    && number == expected_number,
                "unexpected %s syscall number", operation);
    }
  ccw_ir_module_destroy(ir);
}

int
main(void)
{
  ccw_executor *ex = ccw_executor_create();
  CCW_CHECK(ex != NULL, "executor creation failed");
  if (ex == NULL)
    return ccw_test_report("syscall-kernels");
  CCW_CHECK(ccw_host_register_core_accessors(ex) == CCW_OK,
            "core accessor registration failed");

  apply_one(ex, "syscall-x86-64.scm", "syscall.x86-64",
            "x86-64.syscall", CCW_PROFILE_TILLY);
  apply_one(ex, "syscall-x86-64.scm", "syscall.x86-64",
            "x86-64.syscall", CCW_PROFILE_ON1X);
  apply_one(ex, "syscall-aarch64.scm", "syscall.aarch64",
            "aarch64.svc", CCW_PROFILE_TILLY);
  apply_one(ex, "syscall-riscv64.scm", "syscall.riscv64",
            "riscv64.ecall", CCW_PROFILE_ON1X);

  test_io_open(ex, "io-x86-64.scm", "io.x86-64", 2, 4,
               "syscall-x86-64.scm", "syscall.x86-64", "x86-64.syscall");
  test_io_open(ex, "io-aarch64.scm", "io.aarch64", 56, 5,
               "syscall-aarch64.scm", "syscall.aarch64", "aarch64.svc");
  test_io_open(ex, "io-riscv64.scm", "io.riscv64", 56, 5,
               "syscall-riscv64.scm", "syscall.riscv64", "riscv64.ecall");
  test_io_wrapper(ex, "io-x86-64.scm", "io.x86-64", "read", 0);
  test_io_wrapper(ex, "io-x86-64.scm", "io.x86-64", "write", 1);
  test_io_wrapper(ex, "io-x86-64.scm", "io.x86-64", "close", 3);
  test_io_wrapper(ex, "io-aarch64.scm", "io.aarch64", "read", 63);
  test_io_wrapper(ex, "io-aarch64.scm", "io.aarch64", "write", 64);
  test_io_wrapper(ex, "io-aarch64.scm", "io.aarch64", "close", 57);
  test_io_wrapper(ex, "io-riscv64.scm", "io.riscv64", "read", 63);
  test_io_wrapper(ex, "io-riscv64.scm", "io.riscv64", "write", 64);
  test_io_wrapper(ex, "io-riscv64.scm", "io.riscv64", "close", 57);

  {
    ccw_ir *ir = ccw_ir_module_create("builder", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(ir, "main", CCW_TY_I64);
    ccw_node block = ccw_ir_block_add(ir, fn, "entry");
    ccw_node arg = ccw_ir_operand_const_int(ir, CCW_TY_I64, 7);
    ccw_node args[] = { arg };
    ccw_node ins = ccw_ir_build_syscall(ir, block, "result", CCW_TY_I64,
                                        64, args, 1);
    CCW_CHECK(ins != 0 && ccw_ir_instr_operand_count(ir, ins) == 2,
              "C syscall builder must create number plus arguments");
    ccw_ir_module_destroy(ir);
  }

  ccw_executor_destroy(ex);
  return ccw_test_report("syscall-kernels");
}
