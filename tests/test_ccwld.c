#include "ccw_test.h"
#include "../toolchain/ccwld/ccwld.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(void){
  ccwld_error e={0}; ccwld_plan *p=ccwld_plan_new("x86_64-unknown-linux");
  ccwld_output o={"exe","elf","_start",NULL,NULL};
  char *errstr = NULL;
  CCW_CHECK(p != NULL, "plan allocation failed"); CCW_CHECK(ccwld_plan_output(p,&o,&e), "%s", e.message);
  CCW_CHECK(ccwld_plan_input(p,"crt.o",0,1,&e), "%s", e.message);
  CCW_CHECK(ccwld_plan_memory(p,"rom","rx",0x1000,0x1000,&e), "%s", e.message);
  CCW_CHECK(ccwld_plan_section(p,".text","rom",16,NULL,NULL,&e), "%s", e.message);
  ccwld_expr *x=ccwld_expr_binary(CCWLD_OP_ADD,ccwld_expr_dot(),ccwld_expr_int(4));
  uint64_t v=0; CCW_CHECK(ccwld_expr_eval(x,p,0x100,&v,&errstr)&&v==0x104, "%s",
    errstr ? errstr : "expr eval failed");
  free(errstr);
  errstr = NULL;
  CCW_CHECK(ccwld_plan_symbol(p,"__end",x,0,0,&e), "%s", e.message);
  CCW_CHECK(ccwld_plan_seal(p,&e), "%s", e.message);
  char *text=NULL; size_t len=0; CCW_CHECK(ccwld_plan_serialize(p,&text,&len,&e), "%s", e.message);
  CCW_CHECK(len>0 && strstr(text,".text") != NULL, "serialized section missing"); free(text);
  CCW_CHECK(!ccwld_plan_input(p,"late.o",0,0,&e), "sealed plan accepted mutation");
  ccwld_plan_free(p);
  return ccw_test_report("ccwld");
}
