#include "lccwas.h"
#include "lauxlib.h"
#include "lualib.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int grow(ccw_lccwas *c, size_t n) {
  if (c->len + n + 1 <= c->cap) return 1;
  size_t cap = c->cap ? c->cap : 256;
  while (cap < c->len + n + 1) cap *= 2;
  char *p = (char *)realloc(c->data, cap);
  if (!p) return 0;
  c->data = p; c->cap = cap; return 1;
}
static int append(ccw_lccwas *c, const char *s, size_t n) {
  if (!grow(c,n)) return 0;
  memcpy(c->data+c->len,s,n); c->len += n; c->data[c->len]=0; return 1;
}
static int appendf(ccw_lccwas *c, const char *fmt, ...) {
  va_list ap, aq; va_start(ap,fmt); va_copy(aq,ap);
  int n=vsnprintf(NULL,0,fmt,ap); va_end(ap);
  if(n<0 || !grow(c,(size_t)n)){va_end(aq);return 0;}
  vsnprintf(c->data+c->len,(size_t)n+1,fmt,aq); va_end(aq); c->len+=(size_t)n; return 1;
}
static char *dupstr(const char *s) { size_t n=strlen(s)+1; char *p=malloc(n); if(p)memcpy(p,s,n); return p; }
static int fail(char **e, const char *fmt, ...) {
  if (e) { va_list ap,aq; va_start(ap,fmt); va_copy(aq,ap); int n=vsnprintf(NULL,0,fmt,ap); va_end(ap);
    *e=(char*)malloc((size_t)n+1); if(*e)vsnprintf(*e,(size_t)n+1,fmt,aq); va_end(aq); }
  return 0;
}
static ccw_lccwas *self(lua_State *L) { lua_getfield(L,LUA_REGISTRYINDEX,"ccw.lccwas.ctx"); ccw_lccwas *c=(ccw_lccwas*)lua_touserdata(L,-1); lua_pop(L,1); return c; }
static int emit(lua_State *L) {
  ccw_lccwas *c=self(L); if(!c || c->sealed) return luaL_error(L,"lccwas emission buffer is sealed");
  if(c->in_macro) return luaL_error(L,"lccwas.emit is unavailable in macro bodies");
  for(int i=1;i<=lua_gettop(L);i++) {
    int t=lua_type(L,i);
    if(t==LUA_TSTRING) { size_t n; const char *s=lua_tolstring(L,i,&n); if(!append(c,s,n)) return luaL_error(L,"out of memory"); }
    else if(t==LUA_TNUMBER) { if(lua_isinteger(L,i)) { if(!appendf(c,"%lld",(long long)lua_tointeger(L,i))) return luaL_error(L,"out of memory"); } else if(!appendf(c,"%.17g",lua_tonumber(L,i))) return luaL_error(L,"out of memory"); }
    else return luaL_error(L,"lccwas.emit accepts only strings and numbers");
  } return 0;
}
static int emitln(lua_State *L) { int r=emit(L); if(r)return r; ccw_lccwas *c=self(L); if(!append(c,"\n",1))return luaL_error(L,"out of memory"); return 0; }
static int emitf(lua_State *L) {
  luaL_checktype(L,1,LUA_TSTRING); lua_getglobal(L,"string"); lua_getfield(L,-1,"format");
  lua_insert(L,1); lua_remove(L,2); lua_call(L,lua_gettop(L)-1,1); lua_insert(L,1); return emit(L);
}
static int target(lua_State *L) { ccw_lccwas *c=self(L); lua_newtable(L); lua_pushstring(L,c->arch);lua_setfield(L,-2,"arch");lua_pushstring(L,c->syntax);lua_setfield(L,-2,"syntax");lua_pushinteger(L,64);lua_setfield(L,-2,"bits");lua_pushstring(L,"little");lua_setfield(L,-2,"endian");lua_pushinteger(L,8);lua_setfield(L,-2,"ptr_size");return 1; }
static int l_gensym(lua_State *L) { ccw_lccwas *c=self(L); const char *p=luaL_optstring(L,1,"g"); lua_pushfstring(L,".Lccwas_%s_%d",p,(int)c->gensym++); return 1; }
static int l_pos(lua_State *L) { ccw_lccwas *c=self(L); lua_pushstring(L,c->file?c->file:"<template>"); lua_pushinteger(L,1); return 2; }
static int l_error(lua_State *L) { return luaL_error(L,"template: %s",luaL_checkstring(L,1)); }
static int l_warn(lua_State *L) { fprintf(stderr,"ccwas warning: %s\n",luaL_checkstring(L,1)); return 0; }
static int l_data(lua_State *L) {
  ccw_lccwas *c=self(L); const char *dir=luaL_checkstring(L,lua_upvalueindex(1)); if(c->in_macro||c->sealed)return luaL_error(L,"emission unavailable");
  if(!append(c,dir,strlen(dir)))return luaL_error(L,"out of memory");
  for(int i=1;i<=lua_gettop(L);i++){if(!lua_isinteger(L,i))return luaL_error(L,"data helpers require integers"); if(!appendf(c,i==1?" %lld":", %lld",(long long)lua_tointeger(L,i)))return luaL_error(L,"out of memory");}
  append(c,"\n",1); return 0;
}
static int l_bytes(lua_State *L) { ccw_lccwas*c=self(L); size_t n;const unsigned char*s=(const unsigned char*)luaL_checklstring(L,1,&n);append(c,".byte ",6);for(size_t i=0;i<n;i++)appendf(c,i?", 0x%02x":"0x%02x",s[i]);append(c,"\n",1);return 0; }
static int l_zstring(lua_State *L) { int r=l_bytes(L); if(r)return r; ccw_lccwas*c=self(L); append(c,".byte 0\n",8); return 0; }
static int l_label(lua_State *L) { ccw_lccwas*c=self(L);const char*s=luaL_checkstring(L,1);if(!isalpha((unsigned char)*s)&&*s!='_'&&*s!='.')return luaL_error(L,"invalid label");for(const char*p=s+1;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='_'&&*p!='.')return luaL_error(L,"invalid label");appendf(c,"%s:\n",s);return 0; }
static int l_simple(const char *dir, lua_State *L) { ccw_lccwas*c=self(L);const char*s=luaL_checkstring(L,1);appendf(c,"%s %s\n",dir,s);return 0; }
static int l_section(lua_State*L){return l_simple(".section",L);} static int l_global(lua_State*L){return l_simple(".global",L);}
static int l_equ(lua_State*L){ccw_lccwas*c=self(L);appendf(c,".equ %s, %lld\n",luaL_checkstring(L,1),(long long)luaL_checkinteger(L,2));return 0;}
static int l_align(lua_State*L){lua_Integer n=luaL_checkinteger(L,1);if(n<1||(n&(n-1)))return luaL_error(L,"alignment must be a power of two");ccw_lccwas*c=self(L);appendf(c,".align %lld\n",(long long)n);return 0;}
static int l_include(lua_State*L){ccw_lccwas*c=self(L);if(c->in_macro||c->sealed)return luaL_error(L,"lccwas.include unavailable");char *e=NULL;if(!ccw_lccwas_expand_file(c,luaL_checkstring(L,1),&e)){int r=luaL_error(L,"%s",e?e:"include failed");free(e);return r;}return 0;}
static int l_defmacro(lua_State*L){ccw_lccwas*c=self(L);if(c->sealed)return luaL_error(L,"cannot define macro after parse pass");luaL_checkstring(L,1);luaL_checktype(L,2,LUA_TFUNCTION);lua_pushvalue(L,2);lua_setfield(L,LUA_REGISTRYINDEX,luaL_checkstring(L,1));return 0;}
static int l_has(lua_State*L){const char*s=luaL_checkstring(L,1);const char *known[]={"mov","add","sub","imul","ret","nop","push","pop","call","jmp","cmp","test","and","or","xor","shl","shr","sar","lea","load","store",NULL};for(int i=0;known[i];i++)if(!strcasecmp(s,known[i])){lua_pushboolean(L,1);return 1;}lua_pushboolean(L,0);return 1;}
static int l_ext(lua_State*L){lua_newtable(L);lua_pushstring(L,"baseline");lua_rawseti(L,-2,1);return 1;}
static int l_forms(lua_State*L){(void)luaL_checkstring(L,1);lua_newtable(L);lua_newtable(L);lua_pushstring(L,"register");lua_rawseti(L,-2,1);lua_pushstring(L,"register");lua_rawseti(L,-2,2);lua_rawseti(L,-2,1);return 1;}
static int l_requires(lua_State*L){(void)luaL_checkstring(L,1);lua_pushnil(L);return 1;}
static void register_module(ccw_lccwas*c) {
  lua_newtable(c->L);
  lua_pushcfunction(c->L,emit);lua_setfield(c->L,-2,"emit");lua_pushcfunction(c->L,emitln);lua_setfield(c->L,-2,"emitln");lua_pushcfunction(c->L,emitf);lua_setfield(c->L,-2,"emitf");
  lua_pushcfunction(c->L,l_bytes);lua_setfield(c->L,-2,"bytes");lua_pushcfunction(c->L,l_zstring);lua_setfield(c->L,-2,"zstring");
  lua_pushcfunction(c->L,l_label);lua_setfield(c->L,-2,"label");lua_pushcfunction(c->L,l_gensym);lua_setfield(c->L,-2,"gensym");lua_pushcfunction(c->L,l_pos);lua_setfield(c->L,-2,"pos");lua_pushcfunction(c->L,l_error);lua_setfield(c->L,-2,"error");lua_pushcfunction(c->L,l_warn);lua_setfield(c->L,-2,"warn");
  lua_pushcfunction(c->L,l_section);lua_setfield(c->L,-2,"section");lua_pushcfunction(c->L,l_global);lua_setfield(c->L,-2,"global");lua_pushcfunction(c->L,l_equ);lua_setfield(c->L,-2,"equ");lua_pushcfunction(c->L,l_align);lua_setfield(c->L,-2,"align");lua_pushcfunction(c->L,l_include);lua_setfield(c->L,-2,"include");lua_pushcfunction(c->L,l_defmacro);lua_setfield(c->L,-2,"defmacro");
  const char*ds[] = {".byte",".2byte",".4byte",".8byte"}; for(int i=0;i<4;i++){lua_pushstring(c->L,ds[i]);lua_pushcclosure(c->L,l_data,1);lua_setfield(c->L,-2,i==0?"db":i==1?"dw":i==2?"dd":"dq");}
  lua_newtable(c->L);lua_pushcfunction(c->L,l_has);lua_setfield(c->L,-2,"has");lua_pushcfunction(c->L,l_forms);lua_setfield(c->L,-2,"forms");lua_pushcfunction(c->L,l_ext);lua_setfield(c->L,-2,"extensions");lua_pushcfunction(c->L,l_requires);lua_setfield(c->L,-2,"requires");lua_setfield(c->L,-2,"isa");
  lua_pushcfunction(c->L,target);lua_call(c->L,0,1);lua_setfield(c->L,-2,"target");
  lua_newtable(c->L);for(size_t i=0;i<c->env_count;i++){lua_pushstring(c->L,c->env_vals[i]);lua_setfield(c->L,-2,c->env_keys[i]);}lua_setfield(c->L,-2,"env");
  lua_setglobal(c->L,"ccwas");
}
void ccw_lccwas_init(ccw_lccwas*c,const char*arch,const char*syntax,const char*file,int unsafe){memset(c,0,sizeof(*c));c->arch=arch;c->syntax=syntax;c->file=dupstr(file?file:"<input>");c->unsafe=unsafe;c->L=luaL_newstate();if(!c->L)return;luaL_requiref(c->L,LUA_GNAME,luaopen_base,1);lua_pop(c->L,1);luaL_requiref(c->L,LUA_STRLIBNAME,luaopen_string,1);lua_pop(c->L,1);luaL_requiref(c->L,LUA_TABLIBNAME,luaopen_table,1);lua_pop(c->L,1);luaL_requiref(c->L,LUA_MATHLIBNAME,luaopen_math,1);lua_pop(c->L,1);luaL_requiref(c->L,LUA_UTF8LIBNAME,luaopen_utf8,1);lua_pop(c->L,1);if(!unsafe){lua_pushnil(c->L);lua_setglobal(c->L,"io");lua_pushnil(c->L);lua_setglobal(c->L,"os");lua_pushnil(c->L);lua_setglobal(c->L,"package");lua_pushnil(c->L);lua_setglobal(c->L,"require");lua_pushnil(c->L);lua_setglobal(c->L,"load");lua_pushnil(c->L);lua_setglobal(c->L,"loadfile");lua_pushnil(c->L);lua_setglobal(c->L,"dofile");lua_pushnil(c->L);lua_setglobal(c->L,"debug");}lua_pushlightuserdata(c->L,c);lua_setfield(c->L,LUA_REGISTRYINDEX,"ccw.lccwas.ctx");register_module(c);}
void ccw_lccwas_destroy(ccw_lccwas*c){if(c->L)lua_close(c->L);free(c->data);free(c->file);for(size_t i=0;i<c->env_count;i++){free(c->env_keys[i]);free(c->env_vals[i]);}free(c->env_keys);free(c->env_vals);}
int ccw_lccwas_define(ccw_lccwas*c,const char*k,const char*v){char**a=realloc(c->env_keys,(c->env_count+1)*sizeof(*a));char**b=realloc(c->env_vals,(c->env_count+1)*sizeof(*b));if(!a||!b)return 0;c->env_keys=a;c->env_vals=b;c->env_keys[c->env_count]=dupstr(k);c->env_vals[c->env_count]=dupstr(v);if(!c->env_keys[c->env_count]||!c->env_vals[c->env_count])return 0;c->env_count++;if(c->L){lua_getglobal(c->L,"ccwas");if(lua_istable(c->L,-1)){lua_getfield(c->L,-1,"env");if(lua_istable(c->L,-1)){lua_pushstring(c->L,v);lua_setfield(c->L,-2,k);}lua_pop(c->L,1);}lua_pop(c->L,1);}return 1;}
static int run_chunk(ccw_lccwas*c,const char*chunk,size_t n,char**error){char *s=malloc(n+1);if(!s)return fail(error,"out of memory");memcpy(s,chunk,n);s[n]=0;int ok=luaL_loadbuffer(c->L,s,n,c->file)==LUA_OK&&lua_pcall(c->L,0,0,0)==LUA_OK;if(!ok){const char*e=lua_tostring(c->L,-1);int r=fail(error,"%s",e?e:"template error");lua_pop(c->L,1);free(s);return r;}free(s);return 1;}
int ccw_lccwas_expand_buffer(ccw_lccwas*c,const char*src,const char*file,char**error){if(!c||!c->L)return fail(error,"cannot create Lua state");free(c->file);c->file=dupstr(file?file:"<input>");const char*p=src;while(*p){const char*tag=strstr(p,"<?lua");if(!tag){if(!append(c,p,strlen(p)))return fail(error,"out of memory");break;}if(tag>p&&!append(c,p,(size_t)(tag-p)))return fail(error,"out of memory");const char*end=strstr(tag+5,"?>");if(!end)return fail(error,"%s: unterminated <?lua tag",c->file);const char*body=tag+5;while(body<end&&isspace((unsigned char)*body))body++;if(body<end&&*body=='='){body++;while(body<end&&isspace((unsigned char)*body))body++;size_t bn=(size_t)(end-body);char *expr=(char*)malloc(bn+13);if(!expr)return fail(error,"out of memory");memcpy(expr,"ccwas.emit(",11);memcpy(expr+11,body,bn);expr[11+bn]=')';expr[12+bn]=0;int ok=run_chunk(c,expr,12+bn,error);free(expr);if(!ok)return 0;}else if(!run_chunk(c,body,(size_t)(end-body),error))return 0;p=end+2;}return 1;}
int ccw_lccwas_expand_file(ccw_lccwas*c,const char*path,char**error){FILE*f=fopen(path,"rb");if(!f)return fail(error,"%s: %s",path,strerror(errno));fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);char*s=malloc((size_t)n+1);if(!s){fclose(f);return fail(error,"out of memory");}fread(s,1,(size_t)n,f);s[n]=0;fclose(f);int r=ccw_lccwas_expand_buffer(c,s,path,error);free(s);return r;}
char*ccw_lccwas_take_buffer(ccw_lccwas*c){char*p=c->data;c->data=NULL;c->len=c->cap=0;return p;}void ccw_lccwas_seal(ccw_lccwas*c){c->sealed=1;}lua_State*ccw_lccwas_state(ccw_lccwas*c){return c->L;}
