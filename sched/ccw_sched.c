#include "sched.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) { fprintf(stderr, "usage: ccw-sched [--manifest-dir DIR] --check PLAN | --hash PLAN | SCRIPT [PLAN]\n"); }
int main(int argc, char **argv) {
  const char *dir = "manifests", *arg = NULL, *output = NULL; int check = 0, hash = 0, i; ccw_sched_error e; ccw_plan *p;
  for (i=1;i<argc;i++) {
    if (!strcmp(argv[i],"--manifest-dir") && i+1<argc) dir=argv[++i];
    else if (!strcmp(argv[i],"--check") && i+1<argc) { check=1;arg=argv[++i]; }
    else if (!strcmp(argv[i],"--hash") && i+1<argc) { hash=1;arg=argv[++i]; }
    else if (!arg) arg=argv[i]; else if (!output) output=argv[i]; else { usage(); return 2; }
  }
  if (!arg || (check && hash)) { usage(); return 2; }
  if (check) { if (!ccw_plan_check(arg,dir,&e)) { fprintf(stderr,"ccw-sched: %s\n",e.message); return 1; } return 0; }
  if (hash) { FILE *f=fopen(arg,"rb"); long n; char *text, digest[65]; if(!f){perror(arg);return 1;}fseek(f,0,SEEK_END);n=ftell(f);rewind(f);text=malloc((size_t)n+1);if(!text||fread(text,1,(size_t)n,f)!=(size_t)n){fclose(f);free(text);return 1;}text[n]=0;fclose(f);p=ccw_plan_from_text(text);free(text);ccw_plan_hash(p,digest);puts(digest);ccw_plan_free(p);return 0; }
  if (!ccw_sched_run_script(arg,dir,&p,&e)) { fprintf(stderr,"ccw-sched: %s\n",e.message);return 1; }
  if (output) { if(!ccw_plan_write(p,output,&e)){fprintf(stderr,"ccw-sched: %s\n",e.message);ccw_plan_free(p);return 1;} } else fputs(ccw_plan_text(p),stdout);
  ccw_plan_free(p); return 0;
}
