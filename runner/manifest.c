/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Deliberately narrow JSON schema: fixed keys/order, ASCII strings without
 * escapes, no optional/unknown keys. The bundle tool emits this format. */
#include "manifest.h"
#include <string.h>
struct json { const char *p,*end; };
static void ws(struct json *j){while(j->p<j->end && strchr(" \r\n\t",*j->p))j->p++;}
static int literal(struct json *j,const char *s){ws(j);size_t n=strlen(s);if((size_t)(j->end-j->p)<n || memcmp(j->p,s,n))return 0;j->p+=n;return 1;}
static int string(struct json *j,char *out,size_t size){
    if(!literal(j,"\""))return 0;
    size_t n=0;
    while(j->p<j->end && *j->p!='"'){
        unsigned char c=*j->p++;
        if(c<32 || c>126 || c=='\\' || n+1>=size)return 0;
        out[n++]=c;
    }
    out[n]=0;return literal(j,"\"");
}
static int number(struct json *j,unsigned *out){
    ws(j);unsigned n=0,count=0;
    while(j->p<j->end && *j->p>='0' && *j->p<='9'){
        if(count++>8 || n>SD_MAX_BYTES)return 0;
        n=n*10+(*j->p++-'0');
    }
    *out=n;return count && n<=SD_MAX_BYTES;
}
int sd_hex(const char *s,unsigned n){if(strlen(s)!=n)return 0;for(unsigned i=0;i<n;i++)if(!strchr("0123456789abcdef",s[i]))return 0;return 1;}
int sd_path(const char *p){
    size_t n=strlen(p);if(!n || n>=96 || p[0]=='/' || p[n-1]=='/')return 0;
    const char *part=p;
    for(const char *s=p;;s++){
        if(!*s || *s=='/'){
            size_t size=s-part;
            if(!size || (size==1 && *part=='.') || (size==2 && !memcmp(part,"..",2)))return 0;
            if(!*s)break;
            part=s+1;
        }else if(!((*s>='a' && *s<='z') || (*s>='A' && *s<='Z') || (*s>='0' && *s<='9') || strchr("._-",*s)))return 0;
    }
    return strcmp(p,"manifest.json")!=0;
}
int sd_manifest_parse(const char *data,size_t size,struct sd_manifest *m){
    memset(m,0,sizeof(*m));if(!size || size>16384 || memchr(data,0,size))return 0;
    struct json j={data,data+size};unsigned version;
    if(!literal(&j,"{") || !literal(&j,"\"format\"") || !literal(&j,":") || !number(&j,&version) || version!=1 ||
       !literal(&j,",") || !literal(&j,"\"name\"") || !literal(&j,":") || !string(&j,m->name,sizeof(m->name)) ||
       !literal(&j,",") || !literal(&j,"\"firmware_sha256\"") || !literal(&j,":") || !string(&j,m->firmware,sizeof(m->firmware)) ||
       !literal(&j,",") || !literal(&j,"\"files\"") || !literal(&j,":") || !literal(&j,"["))return 0;
    if(!m->name[0] || (strcmp(m->firmware,"any") && !sd_hex(m->firmware,64)))return 0;
    for(char *s=m->name;*s;s++)if(!((*s>='a' && *s<='z') || (*s>='A' && *s<='Z') || (*s>='0' && *s<='9') || strchr("._-",*s)))return 0;
    int start=0,stop=0;
    do {
        if(m->count==SD_MAX_FILES)return 0;
        struct sd_file *f=&m->files[m->count];
        if(!literal(&j,"{") || !literal(&j,"\"path\"") || !literal(&j,":") || !string(&j,f->path,sizeof(f->path)) ||
           !literal(&j,",") || !literal(&j,"\"size\"") || !literal(&j,":") || !number(&j,&f->size) ||
           !literal(&j,",") || !literal(&j,"\"sha256\"") || !literal(&j,":") || !string(&j,f->sha256,sizeof(f->sha256)) || !literal(&j,"}"))return 0;
        if(!sd_path(f->path) || !sd_hex(f->sha256,64) || f->size>1024*1024 || f->size>SD_MAX_BYTES-m->bytes)return 0;
        for(unsigned i=0;i<m->count;i++)if(!strcmp(m->files[i].path,f->path))return 0;
        m->bytes+=f->size;m->count++;
        start|=!strcmp(f->path,"start.sh");stop|=!strcmp(f->path,"stop.sh");
    }while(literal(&j,","));
    if(!literal(&j,"]") || !literal(&j,"}"))return 0;
    ws(&j);return j.p==j.end && start && stop;
}
