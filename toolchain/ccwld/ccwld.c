#include "ccwld.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

typedef struct { char *path; int needed, startup; } input_rec;
typedef struct { char *name,*attrs; uint64_t origin,length; } mem_rec;
typedef struct { char *name,*region,*at_region,*selector; uint64_t align; int load; } sec_rec;
typedef struct { char *name; ccwld_expr *expr; int provide,hidden; } sym_rec;
typedef struct { ccwld_phase phase; int (*fn)(ccwld_phase,ccwld_link*,void*); void *user; } hook_rec;
struct ccwld_expr { int kind; uint64_t value; char *name; char op; struct ccwld_expr *a,*b; };
struct ccwld_plan {
  char *target; ccwld_output output; int sealed; unsigned gensym;
  input_rec *inputs; size_t ni,ci; char **paths; size_t np,cp;
  mem_rec *mem; size_t nm,cm; sec_rec *secs; size_t ns,cs;
  sym_rec *syms; size_t ny,cy; hook_rec *hooks; size_t nh,ch;
  char *serialized;
};
struct ccwld_link { ccwld_plan *plan; ccwld_phase phase; };
static void err(ccwld_error *e,int c,const char *f,...){ if(!e)return; e->code=c; va_list ap;va_start(ap,f);vsnprintf(e->message,sizeof(e->message),f,ap);va_end(ap); }
static char *dup(const char *s){ return s?strdup(s):NULL; }
static int grow(void **p,size_t *cap,size_t n,size_t sz){ if(n<=*cap)return 1; size_t c=*cap?*cap*2:8; while(c<n)c*=2; void *q=realloc(*p,c*sz); if(!q)return 0; *p=q;*cap=c;return 1; }
static int validfmt(const char *f){return !f||!strcmp(f,"elf")||!strcmp(f,"pe")||!strcmp(f,"macho");}
static int validkind(const char *k){return k && (!strcmp(k,"exe")||!strcmp(k,"dso")||!strcmp(k,"reloc")||!strcmp(k,"pie"));}
ccwld_plan *ccwld_plan_new(const char *target){ccwld_plan*p=calloc(1,sizeof(*p));if(p)p->target=dup(target?target:"unknown");return p;}
void ccwld_expr_free(ccwld_expr *e){if(!e)return;ccwld_expr_free(e->a);ccwld_expr_free(e->b);free(e->name);free(e);}
void ccwld_plan_free(ccwld_plan*p){size_t i;if(!p)return;free(p->target);free(p->serialized);for(i=0;i<p->ni;i++)free(p->inputs[i].path);free(p->inputs);for(i=0;i<p->np;i++)free(p->paths[i]);free(p->paths);for(i=0;i<p->nm;i++){free(p->mem[i].name);free(p->mem[i].attrs);}free(p->mem);for(i=0;i<p->ns;i++){free(p->secs[i].name);free(p->secs[i].region);free(p->secs[i].at_region);free(p->secs[i].selector);}free(p->secs);for(i=0;i<p->ny;i++){free(p->syms[i].name);ccwld_expr_free(p->syms[i].expr);}free(p->syms);free(p->hooks);free((void *)p->output.kind);free((void *)p->output.format);free((void *)p->output.entry);free((void *)p->output.soname);free((void *)p->output.osabi);free(p);}
void ccwld_free(void*p){free(p);}
int ccwld_plan_output(ccwld_plan*p,const ccwld_output*o,ccwld_error*e){if(!p||!o||p->sealed){err(e,1,"plan is sealed or invalid");return 0;}if(!validkind(o->kind)||!validfmt(o->format)){err(e,2,"invalid output kind or format");return 0;}if(o->soname&&o->format&&strcmp(o->format,"elf")){err(e,2,"soname is only valid for ELF");return 0;}free((void *)p->output.kind);free((void *)p->output.format);free((void *)p->output.entry);free((void *)p->output.soname);free((void *)p->output.osabi);p->output=(ccwld_output){dup(o->kind),dup(o->format?o->format:"elf"),dup(o->entry),dup(o->soname),dup(o->osabi)};return 1;}
int ccwld_plan_input(ccwld_plan*p,const char*x,int n,int s,ccwld_error*e){if(!p||p->sealed||!x){err(e,1,"invalid input");return 0;}if(!grow((void**)&p->inputs,&p->ci,p->ni+1,sizeof(*p->inputs))){err(e,3,"out of memory");return 0;}p->inputs[p->ni++]=(input_rec){dup(x),n,s};return 1;}
int ccwld_plan_search_path(ccwld_plan*p,const char*x,ccwld_error*e){if(!p||p->sealed||!x){err(e,1,"invalid search path");return 0;}if(!grow((void**)&p->paths,&p->cp,p->np+1,sizeof(*p->paths))){err(e,3,"out of memory");return 0;}p->paths[p->np++]=dup(x);return 1;}
int ccwld_plan_memory(ccwld_plan*p,const char*n,const char*a,uint64_t o,uint64_t l,ccwld_error*e){if(!p||p->sealed||!n||!l){err(e,1,"invalid memory region");return 0;}if(!grow((void**)&p->mem,&p->cm,p->nm+1,sizeof(*p->mem))){err(e,3,"out of memory");return 0;}p->mem[p->nm++]=(mem_rec){dup(n),dup(a?a:""),o,l};return 1;}
int ccwld_plan_section(ccwld_plan*p,const char*n,const char*r,uint64_t al,const char*sel,const char*at,ccwld_error*e){if(!p||p->sealed||!n){err(e,1,"invalid section");return 0;}if(!grow((void**)&p->secs,&p->cs,p->ns+1,sizeof(*p->secs))){err(e,3,"out of memory");return 0;}p->secs[p->ns++]=(sec_rec){dup(n),dup(r),dup(at),dup(sel),al?al:1,1};return 1;}
int ccwld_plan_symbol(ccwld_plan*p,const char*n,ccwld_expr*x,int provide,int hidden,ccwld_error*e){if(!p||p->sealed||!n||!x){err(e,1,"invalid symbol");return 0;}if(!grow((void**)&p->syms,&p->cy,p->ny+1,sizeof(*p->syms))){err(e,3,"out of memory");return 0;}p->syms[p->ny++]=(sym_rec){dup(n),x,provide,hidden};return 1;}
int ccwld_plan_hook(ccwld_plan*p,ccwld_phase ph,int(*fn)(ccwld_phase,ccwld_link*,void*),void*u,ccwld_error*e){if(!p||p->sealed||ph<1||ph>4||!fn){err(e,1,"invalid hook");return 0;}if(!grow((void**)&p->hooks,&p->ch,p->nh+1,sizeof(*p->hooks))){err(e,3,"out of memory");return 0;}p->hooks[p->nh++]=(hook_rec){ph,fn,u};return 1;}
static ccwld_expr *ex(int k){ccwld_expr*x=calloc(1,sizeof(*x));if(x)x->kind=k;return x;}
ccwld_expr*ccwld_expr_int(uint64_t v){ccwld_expr*x=ex(1);if(x)x->value=v;return x;}
ccwld_expr*ccwld_expr_symbol(const char*n){ccwld_expr*x=ex(2);if(x)x->name=dup(n);return x;}
ccwld_expr*ccwld_expr_dot(void){return ex(3);}
ccwld_expr*ccwld_expr_binary(char op,ccwld_expr*a,ccwld_expr*b){ccwld_expr*x=ex(4);if(x){x->op=op;x->a=a;x->b=b;}return x;}
ccwld_expr*ccwld_expr_unary(char op,ccwld_expr*a){ccwld_expr*x=ex(5);if(x){x->op=op;x->a=a;}return x;}
ccwld_expr*ccwld_expr_align(ccwld_expr*a,uint64_t n){return ccwld_expr_binary('A',a,ccwld_expr_int(n));}
static int eval(const ccwld_expr*x,const ccwld_plan*p,uint64_t dot,uint64_t*out,ccwld_error*e){size_t i;uint64_t a,b;if(!x){err(e,4,"null expression");return 0;}switch(x->kind){case 1:*out=x->value;return 1;case 3:*out=dot;return 1;case 2:for(i=0;i<p->ny;i++)if(!strcmp(p->syms[i].name,x->name)){if(!eval(p->syms[i].expr,p,dot,out,e)){return 0;}return 1;}err(e,4,"undefined symbol '%s'",x->name);return 0;case 5:if(!eval(x->a,p,dot,&a,e))return 0;*out=x->op=='-'?(uint64_t)(-(int64_t)a):~a;return 1;case 4:if(!eval(x->a,p,dot,&a,e)||!eval(x->b,p,dot,&b,e))return 0;if(x->op=='A'){*out=(a+b-1)/b*b;return 1;}switch(x->op){case'+':*out=a+b;break;case'-':*out=a-b;break;case'*':*out=a*b;break;case'/':if(!b){err(e,4,"division by zero");return 0;}*out=a/b;break;case'&':*out=a&b;break;case'|':*out=a|b;break;case'^':*out=a^b;break;default:err(e,4,"unknown expression operator");return 0;}return 1;}err(e,4,"unknown expression");return 0;}
int ccwld_expr_eval(const ccwld_expr*x,const ccwld_plan*p,uint64_t d,uint64_t*out,ccwld_error*e){return eval(x,p,d,out,e);}
static void append(char **s,size_t *n,size_t *c,const char*f,...){va_list ap;char b[512];int z;va_start(ap,f);z=vsnprintf(b,sizeof(b),f,ap);va_end(ap);if(z<0)return;if(*n+(size_t)z+1>*c){size_t nc=*c?*c*2:1024;while(nc<*n+(size_t)z+1)nc*=2;*s=realloc(*s,nc);*c=nc;}memcpy(*s+*n,b,(size_t)z);*n+=(size_t)z;(*s)[*n]=0;}
static void exprstr(const ccwld_expr*x,char **s,size_t*n,size_t*c){if(!x){append(s,n,c,"null");return;}if(x->kind==1){append(s,n,c,"%llu",(unsigned long long)x->value);return;}if(x->kind==2){append(s,n,c,"sym(%s)",x->name);return;}if(x->kind==3){append(s,n,c,"dot");return;}if(x->kind==5){append(s,n,c,"(%c",x->op);exprstr(x->a,s,n,c);append(s,n,c,")");return;}append(s,n,c,"(");exprstr(x->a,s,n,c);append(s,n,c,"%c",x->op);exprstr(x->b,s,n,c);append(s,n,c,")");}
int ccwld_plan_serialize(const ccwld_plan*p,char**out,size_t*len,ccwld_error*e){size_t i,n=0,c=0;if(!p||!out){err(e,1,"invalid serialization request");return 0;}char*s=NULL;append(&s,&n,&c,"{\"target\":\"%s\",\"output\":{\"kind\":\"%s\",\"format\":\"%s\",\"entry\":\"%s\"},\"inputs\":[",p->target,p->output.kind?p->output.kind:"",p->output.format?p->output.format:"",p->output.entry?p->output.entry:"");for(i=0;i<p->ni;i++)append(&s,&n,&c,"%s{\"path\":\"%s\",\"as_needed\":%s,\"startup\":%s}",i?",":"",p->inputs[i].path,p->inputs[i].needed?"true":"false",p->inputs[i].startup?"true":"false");append(&s,&n,&c,"],\"memory\":[");for(i=0;i<p->nm;i++)append(&s,&n,&c,"%s{\"name\":\"%s\",\"attrs\":\"%s\",\"origin\":%llu,\"length\":%llu}",i?",":"",p->mem[i].name,p->mem[i].attrs,(unsigned long long)p->mem[i].origin,(unsigned long long)p->mem[i].length);append(&s,&n,&c,"],\"sections\":[");for(i=0;i<p->ns;i++)append(&s,&n,&c,"%s{\"name\":\"%s\",\"region\":\"%s\",\"align\":%llu}",i?",":"",p->secs[i].name,p->secs[i].region?p->secs[i].region:"",(unsigned long long)p->secs[i].align);append(&s,&n,&c,"],\"symbols\":[");for(i=0;i<p->ny;i++){append(&s,&n,&c,"%s{\"name\":\"%s\",\"provide\":%s,\"hidden\":%s,\"expr\":\"",i?",":"",p->syms[i].name,p->syms[i].provide?"true":"false",p->syms[i].hidden?"true":"false");exprstr(p->syms[i].expr,&s,&n,&c);append(&s,&n,&c,"\"}");}append(&s,&n,&c,"]}");*out=s;if(len)*len=n;return 1;}
int ccwld_plan_seal(ccwld_plan*p,ccwld_error*e){char*s;size_t n;if(!p||p->sealed){err(e,1,"plan already sealed");return 0;}if(!p->output.kind){err(e,2,"output declaration is required");return 0;}if(!ccwld_plan_serialize(p,&s,&n,e))return 0;free(p->serialized);p->serialized=s;p->sealed=1;return 1;}
int ccwld_plan_hash(const ccwld_plan*p,char out[65]){uint64_t h=1469598103934665603ULL;size_t i;if(!p||!out)return 0;const char*s=p->serialized;if(!s){if(!ccwld_plan_serialize(p,(char**)&s,&i,NULL))return 0;}for(i=0;s[i];i++){h^=(unsigned char)s[i];h*=1099511628211ULL;}snprintf(out,65,"%016llx%016llx%016llx%016llx",(unsigned long long)h,(unsigned long long)(h^0x9e3779b97f4a7c15ULL),(unsigned long long)(h*33),(unsigned long long)(~h));if(!p->serialized)free((void*)s);return 1;}
static int phase(ccwld_plan*p,ccwld_phase ph,ccwld_error*e){size_t i;ccwld_link l={p,ph};for(i=0;i<p->nh;i++)if(p->hooks[i].phase==ph&&p->hooks[i].fn(ph,&l,p->hooks[i].user)!=0){err(e,5,"hook failed at phase %d",ph);return 0;}return 1;}
int ccwld_link_run(ccwld_plan*p,const char*out,ccwld_error*e){FILE*f;char hash[65];char*s;size_t n;if(!p||!out){err(e,1,"invalid link request");return 0;}if(!p->sealed&&!ccwld_plan_seal(p,e))return 0;if(!phase(p,CCWLD_PHASE_RESOLVED,e)||!phase(p,CCWLD_PHASE_GC,e)||!phase(p,CCWLD_PHASE_LAYOUT,e)||!phase(p,CCWLD_PHASE_EMIT,e))return 0;if(!ccwld_plan_hash(p,hash)||!ccwld_plan_serialize(p,&s,&n,e))return 0;f=fopen(out,"wb");if(!f){free(s);err(e,6,"cannot write output '%s'",out);return 0;}fprintf(f,"CCWLD-OBJECT\nformat=%s\nkind=%s\nplan-hash=%s\nreproducible=true\n.note.ccw=%s\n",p->output.format,p->output.kind,hash,hash);fwrite(s,1,n,f);fputc('\n',f);fclose(f);free(s);return 1;}
int ccwld_link_files(const char *target, const char *output,
                     const char *const *inputs, size_t input_count,
                     const ccwld_link_options *options, ccwld_error *e)
{
    ccwld_plan *p = ccwld_plan_new(target);
    ccwld_output out;
    int ok = 0;
    if (!p || !output || (!inputs && input_count)) {
        ccwld_plan_free(p);
        err(e, 1, "invalid link request");
        return 0;
    }
    out.kind = options && options->kind ? options->kind : "exe";
    out.format = options && options->format ? options->format : "elf";
    out.entry = options ? options->entry : NULL;
    out.soname = options ? options->soname : NULL;
    out.osabi = options ? options->osabi : NULL;
    if (!ccwld_plan_output(p, &out, e)) goto done;
    if (options) {
        for (size_t i = 0; i < options->search_path_count; ++i)
            if (!ccwld_plan_search_path(p, options->search_paths[i], e)) goto done;
    }
    for (size_t i = 0; i < input_count; ++i)
        if (!ccwld_plan_input(p, inputs[i], 0, i == 0, e)) goto done;
    ok = ccwld_link_run(p, output, e);
done:
    ccwld_plan_free(p);
    return ok;
}
