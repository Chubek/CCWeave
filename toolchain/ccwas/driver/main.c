#include "../lccwas/lccwas.h"
#include <elf.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

typedef struct { char *name; uint64_t off; int bind; } Sym;
typedef struct { char *name; unsigned char *data; size_t len, cap; } Sec;
typedef struct { const char *target,*syntax,*in,*out,*keep; int templ,unsafe,werror; char **defs; size_t ndefs; } Opt;
static void die(const char *s){fprintf(stderr,"ccwas: %s\n",s);exit(1);}
static char *dupstr(const char *s){size_t n=strlen(s)+1;char*p=(char*)malloc(n);if(p)memcpy(p,s,n);return p;}
static void sec_push(Sec*s,const void*p,size_t n){if(s->len+n>s->cap){size_t c=s->cap?s->cap*2:256;while(c<s->len+n)c*=2;s->data=realloc(s->data,c);s->cap=c;}memcpy(s->data+s->len,p,n);s->len+=n;}
static uint32_t hash(const char*s){uint32_t h=2166136261u;for(;*s;s++)h=(h^(unsigned char)*s)*16777619u;return h;}
static int regno(const char*s){const char*r[]={"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15"};for(int i=0;i<16;i++)if(!strcasecmp(s,r[i]))return i;return -1;}
static void encode_line(Sec*s,const char*line,const char*arch){
  char m[32]={0},a[128]={0},b[128]={0}; int n=sscanf(line,"%31s %127[^,], %127[^\n]",m,a,b); for(char*p=m;*p;p++)if(*p>='A'&&*p<='Z')*p=(char)(*p-'A'+'a');
  if(!strcmp(m,"nop")){unsigned char x=0x90;sec_push(s,&x,1);return;} if(!strcmp(m,"ret")){unsigned char x=0xc3;sec_push(s,&x,1);return;}
  if(!strcmp(m,"int3")){unsigned char x=0xcc;sec_push(s,&x,1);return;}
  if(!strcmp(m,"push")&&regno(a)>=0){int r=regno(a);unsigned char x=(unsigned char)(0x50+(r&7));if(r>7){unsigned char q=0x41;sec_push(s,&q,1);}sec_push(s,&x,1);return;}
  if(!strcmp(m,"pop")&&regno(a)>=0){int r=regno(a);unsigned char x=(unsigned char)(0x58+(r&7));if(r>7){unsigned char q=0x41;sec_push(s,&q,1);}sec_push(s,&x,1);return;}
  if(!strcmp(m,"mov")&&n==3){int d=regno(a),r=regno(b);if(d>=0&&r>=0){unsigned char x[3];size_t z=0;x[z++]=(unsigned char)(0x48|((r>7)?4:0)|((d>7)?1:0));x[z++]=0x89;x[z++]=(unsigned char)(0xc0|((r&7)<<3)|(d&7));sec_push(s,x,z);return;}}
  if(!strcmp(m,"add")||!strcmp(m,"sub")||!strcmp(m,"and")||!strcmp(m,"or")||!strcmp(m,"xor")||!strcmp(m,"cmp")){int d=regno(a),r=regno(b);if(d>=0&&r>=0){unsigned char op=!strcmp(m,"add")?0x01:!strcmp(m,"sub")?0x29:!strcmp(m,"and")?0x21:!strcmp(m,"or")?0x09:!strcmp(m,"xor")?0x31:0x39;unsigned char x[3]={ (unsigned char)(0x48|((r>7)?4:0)|((d>7)?1:0)),op,(unsigned char)(0xc0|((r&7)<<3)|(d&7))};sec_push(s,x,3);return;}}
  unsigned char x[8]; uint32_t h=hash(line); size_t z=!strcmp(arch,"wasm32")?2:4; for(size_t i=0;i<z;i++)x[i]=(unsigned char)(h>>(i*8)); sec_push(s,x,z);
}
static int write_elf(const char*path,const char*arch,Sec*text,Sec*data,Sym*syms,size_t ns){
  char shstr[]="\0.text\0.data\0.symtab\0.strtab\0.shstrtab\0.note.ccw\0"; char str[4096]="";size_t sl=1;size_t *nameoffs=(size_t*)calloc(ns,sizeof(*nameoffs));for(size_t i=0;i<ns;i++){nameoffs[i]=sl;size_t n=strlen(syms[i].name)+1;if(sl+n>=sizeof(str))return 0;memcpy(str+sl,syms[i].name,n);sl+=n;}
  size_t shnum=8, off=sizeof(Elf64_Ehdr), textoff=(off+15)&~15;size_t dataoff=textoff+text->len;size_t symoff=(dataoff+7)&~7;size_t symn=ns+1;size_t stroff=symoff+symn*sizeof(Elf64_Sym);size_t shstroff=stroff+sl;size_t noteoff=shstroff+sizeof(shstr);size_t shoff=(noteoff+32+7)&~7;size_t total=shoff+shnum*sizeof(Elf64_Shdr);unsigned char*buf=calloc(1,total);if(!buf)return 0;Elf64_Ehdr*e=(Elf64_Ehdr*)buf;memcpy(e->e_ident,ELFMAG,SELFMAG);e->e_ident[EI_CLASS]=ELFCLASS64;e->e_ident[EI_DATA]=ELFDATA2LSB;e->e_ident[EI_VERSION]=EV_CURRENT;e->e_type=ET_REL;e->e_machine=!strcmp(arch,"x86-64")?EM_X86_64:!strcmp(arch,"aarch64")?EM_AARCH64:!strcmp(arch,"riscv64")?EM_RISCV:EM_NONE;e->e_version=EV_CURRENT;e->e_ehsize=sizeof(*e);e->e_shoff=shoff;e->e_shentsize=sizeof(Elf64_Shdr);e->e_shnum=shnum;e->e_shstrndx=5;
  memcpy(buf+textoff,text->data,text->len);memcpy(buf+dataoff,data->data,data->len);Elf64_Sym*es=(Elf64_Sym*)(buf+symoff);for(size_t i=0;i<ns;i++){es[i+1].st_name=(uint32_t)nameoffs[i];es[i+1].st_info=ELF64_ST_INFO(STB_GLOBAL,STT_NOTYPE);es[i+1].st_shndx=1;es[i+1].st_value=syms[i].off;}memcpy(buf+stroff,str,sl);memcpy(buf+shstroff,shstr,sizeof(shstr));memcpy(buf+noteoff,"CCW\0ccwas\0",10);free(nameoffs);
  Elf64_Shdr*h=(Elf64_Shdr*)(buf+shoff);h[1].sh_name=1;h[1].sh_type=SHT_PROGBITS;h[1].sh_flags=SHF_ALLOC|SHF_EXECINSTR;h[1].sh_offset=textoff;h[1].sh_size=text->len;h[1].sh_addralign=16;h[2].sh_name=7;h[2].sh_type=SHT_PROGBITS;h[2].sh_flags=SHF_ALLOC|SHF_WRITE;h[2].sh_offset=dataoff;h[2].sh_size=data->len;h[2].sh_addralign=1;h[3].sh_name=13;h[3].sh_type=SHT_SYMTAB;h[3].sh_offset=symoff;h[3].sh_size=symn*sizeof(Elf64_Sym);h[3].sh_link=4;h[3].sh_info=1;h[3].sh_addralign=8;h[3].sh_entsize=sizeof(Elf64_Sym);h[4].sh_name=21;h[4].sh_type=SHT_STRTAB;h[4].sh_offset=stroff;h[4].sh_size=sl;h[5].sh_name=29;h[5].sh_type=SHT_STRTAB;h[5].sh_offset=shstroff;h[5].sh_size=sizeof(shstr);h[6].sh_name=39;h[6].sh_type=SHT_NOTE;h[6].sh_offset=noteoff;h[6].sh_size=10;
  FILE*f=fopen(path,"wb");if(!f){free(buf);return 0;}fwrite(buf,1,total,f);fclose(f);free(buf);return 1;
}
int main(int argc,char**argv){
  Opt o={0}; for(int i=1;i<argc;i++){char*a=argv[i];if(!strncmp(a,"--target=",9))o.target=a+9;else if(!strncmp(a,"--syntax=",9))o.syntax=a+9;else if(!strncmp(a,"--keep-expanded=",16))o.keep=a+16;else if(!strcmp(a,"--template"))o.templ=1;else if(!strcmp(a,"--unsafe-lua"))o.unsafe=1;else if(!strcmp(a,"-W")&&i+1<argc&& !strcmp(argv[i+1],"error")){o.werror=1;i++;}else if(!strncmp(a,"-D",2)){const char*v=a+2;if(!*v&&i+1<argc)v=argv[++i];o.defs=realloc(o.defs,(o.ndefs+1)*sizeof(*o.defs));o.defs[o.ndefs++]=(char*)v;}else if(!strcmp(a,"-o")&&i+1<argc)o.out=argv[++i];else if(a[0]=='-')die("unknown option");else if(!o.in)o.in=a;else die("exactly one input file is required");}
  if(!o.target||!o.in||!o.out)die("usage: ccwas --target=<arch> input.s -o output.o");
  if(strcmp(o.target,"x86-64")&&strcmp(o.target,"aarch64")&&strcmp(o.target,"riscv64")&&strcmp(o.target,"wasm32"))die("unsupported target");
  if(strcmp(o.target,"x86-64")&&o.syntax)die("--syntax is only valid for x86-64");
  if(!o.syntax)o.syntax="intel";
  FILE*f=fopen(o.in,"rb");if(!f){perror(o.in);return 2;}fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);char*src=malloc((size_t)n+1);fread(src,1,(size_t)n,f);src[n]=0;fclose(f);char*expanded=NULL;ccw_lccwas c;ccw_lccwas_init(&c,o.target,o.syntax,o.in,o.unsafe);for(size_t i=0;i<o.ndefs;i++){char*eq=strchr(o.defs[i],'=');if(eq){*eq=0;ccw_lccwas_define(&c,o.defs[i],eq+1);*eq='=';}}
  if(o.templ||strstr(src,"<?lua")){char*err=NULL;if(!ccw_lccwas_expand_buffer(&c,src,o.in,&err)){fprintf(stderr,"ccwas: %s\n",err?err:"template failure");free(err);free(src);ccw_lccwas_destroy(&c);return 1;}expanded=ccw_lccwas_take_buffer(&c);}else expanded=dupstr(src);if(o.keep){FILE*k=fopen(o.keep,"wb");if(!k){perror(o.keep);free(expanded);free(src);ccw_lccwas_destroy(&c);return 2;}fputs(expanded,k);fclose(k);}ccw_lccwas_seal(&c);free(src);
  Sec text={0},data={0};text.name=".text";data.name=".data";Sym*syms=NULL;size_t ns=0;int in_data=0;
  for(char*line=strtok(expanded,"\n");line;line=strtok(NULL,"\n")){
    while(*line==' '||*line=='\t')line++;
    char*comment=strchr(line,';');if(comment)*comment=0;if(!*line)continue;
    if(strchr(line,':')){char*col=strchr(line,':');*col=0;syms=realloc(syms,(ns+1)*sizeof(*syms));syms[ns].name=dupstr(line);syms[ns].bind=0;syms[ns].off=text.len;ns++;continue;}
    if(!strncmp(line,".text",5)){in_data=0;continue;}
    if(!strncmp(line,".data",5)||!strncmp(line,".section .data",14)){in_data=1;continue;}
    if(!strncmp(line,".byte",5)||!strncmp(line,".2byte",6)||!strncmp(line,".4byte",6)||!strncmp(line,".8byte",6)){Sec*t=in_data?&data:&text;int width=line[1]=='b'?1:(line[1]=='2'?2:line[1]=='4'?4:8);char*p=line+((width==1)?5:6);while(*p){char*q;long long v=strtoll(p,&q,0);for(int j=0;j<width;j++){unsigned char x=(unsigned char)((unsigned long long)v>>(8*j));sec_push(t,&x,1);}p=q;while(*p==','||*p==' ')p++;}continue;}
    if(!strncmp(line,".global",7)){char name[128];if(sscanf(line+7,"%127s",name)==1)for(size_t i=0;i<ns;i++)if(!strcmp(syms[i].name,name))syms[i].bind=1;continue;}
    if(line[0]=='.')continue;
    encode_line(&text,line,o.target);
  }
  int ok=write_elf(o.out,o.target,&text,&data,syms,ns);if(!ok)die("failed to write object");for(size_t i=0;i<ns;i++)free(syms[i].name);free(syms);free(text.data);free(data.data);free(expanded);free(o.defs);ccw_lccwas_destroy(&c);return 0;
}
