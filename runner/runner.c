/* SPDX-License-Identifier: GPL-3.0-or-later */
/* HU-side one-shot controller. No persistent installation or update mechanism.
 * Only manifest-listed, copied, hash-verified RAM files may be executed. */
#include "manifest.h"
#include "sha256.h"
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef SD_FS
#define SD_FS "/fs"
#endif
#ifndef SD_RAM
#define SD_RAM "/ramdisk/pico-manager"
#endif
#ifndef SD_FIRMWARE
#define SD_FIRMWARE "/ifs/lsd.jxe"
#endif
#ifndef SD_SCRIPT_TIMEOUT
#define SD_SCRIPT_TIMEOUT 180000
#endif
#ifndef SD_STOP_TIMEOUT
#define SD_STOP_TIMEOUT 30000
#endif
static char firmware[65];
void sd_hash_progress(const char *path,unsigned bytes){
    if(!strcmp(path,SD_FIRMWARE))printf("PICOSD_PHASE identifying-firmware-%u-MiB\n",bytes/(1024*1024));
}
struct card {char mount[48],digest[65];struct sd_manifest manifest;};
static struct card cards[8];static unsigned card_count;
static int output(const char *s){printf("PICOSD_ERROR %s\n",s);return 1;}
static int path_join(char *p,size_t size,const char *a,const char *b){int n=snprintf(p,size,"%s/%s",a,b);return n>0 && (size_t)n<size;}
static int directory(const char *p){struct stat st;return !lstat(p,&st) && S_ISDIR(st.st_mode);}
static int safe_file(const char *p){
    char copy[512];if(strlen(p)>=sizeof(copy))return 0;strcpy(copy,p);
    for(char *c=copy+1;*c;c++)if(*c=='/'){
        *c=0;int ok=directory(copy);*c='/';if(!ok)return 0;
    }
    struct stat st;return !lstat(p,&st) && S_ISREG(st.st_mode);
}
static int read_manifest(const char *mount,struct card *card){
    char base[256],path[512];struct stat st;char data[16385];
    if(!path_join(base,sizeof(base),SD_FS,mount) || !directory(base) ||
       !path_join(path,sizeof(path),base,"mhi2/manifest.json") || !safe_file(path) ||
       stat(path,&st) || st.st_size<=0 || st.st_size>16384)return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    size_t n=fread(data,1,sizeof(data),f);int ok=!ferror(f) && n==(size_t)st.st_size;fclose(f);
    if(!ok || !sd_manifest_parse(data,n,&card->manifest) || !sd_sha256(path,card->digest))return 0;
    /* Hash the parsed bytes as a RAM file, avoiding approval of bytes different
     * from the manifest read during a card replacement. */
    f=fopen(SD_RAM "/manifest-read","wb");if(!f)return 0;
    ok=fwrite(data,1,n,f)==n;if(fclose(f))ok=0;
    char digest[65];ok=ok && sd_sha256(SD_RAM "/manifest-read",digest) && !strcmp(digest,card->digest);
    unlink(SD_RAM "/manifest-read");
    if(ok)strcpy(card->mount,mount);
    return ok;
}
static int cmp_card(const void *a,const void *b){return strcmp(((const struct card *)a)->mount,((const struct card *)b)->mount);}
static int scan(void){
    DIR *d=opendir(SD_FS);if(!d)return output("sd-mount-directory-unavailable");
    struct dirent *e;card_count=0;
    while((e=readdir(d))){
        /* Stock readers mount as sd*; USB storage is intentionally excluded. */
        if(strncmp(e->d_name,"sd",2) || strlen(e->d_name)>=sizeof(cards[0].mount) || !sd_path(e->d_name))continue;
        struct card card;
        if(!read_manifest(e->d_name,&card))continue;
        if(card_count==8){closedir(d);return output("too-many-sd-bundles");}
        cards[card_count++]=card;
    }
    closedir(d);qsort(cards,card_count,sizeof(cards[0]),cmp_card);
    for(unsigned i=0;i<card_count;i++)printf("PICOSD_CARD %s %s %s %s\n",cards[i].mount,cards[i].digest,cards[i].manifest.name,
        !strcmp(cards[i].manifest.firmware,"any") || !strcmp(cards[i].manifest.firmware,firmware)?"compatible":"unsupported");
    printf("PICOSD_COUNT %u\n",card_count);return 0;
}
static int remove_tree(const char *path){
    struct stat st;if(lstat(path,&st))return errno==ENOENT;
    if(!S_ISDIR(st.st_mode))return unlink(path)==0;
    DIR *d=opendir(path);if(!d)return 0;struct dirent *e;int ok=1;
    while((e=readdir(d)))if(strcmp(e->d_name,".") && strcmp(e->d_name,"..")){
        char p[512];if(!path_join(p,sizeof(p),path,e->d_name) || !remove_tree(p)){ok=0;break;}
    }
    closedir(d);return ok && !rmdir(path);
}
static int parents(char *path){
    for(char *p=path+strlen(SD_RAM)+1;*p;p++)if(*p=='/'){
        *p=0;int ok=(!mkdir(path,0700) || errno==EEXIST) && directory(path);*p='/';if(!ok)return 0;
    }
    return 1;
}
static int copy_verified(const char *source,const char *dest,const struct sd_file *file){
    struct stat before,after;
    if(!safe_file(source) || lstat(source,&before) || before.st_size<0 || (unsigned long)before.st_size!=file->size)return 0;
    int in=open(source,O_RDONLY);if(in<0)return 0;
    if(fstat(in,&after) || !S_ISREG(after.st_mode) || before.st_ino!=after.st_ino || before.st_dev!=after.st_dev){close(in);return 0;}
    int out=open(dest,O_WRONLY|O_CREAT|O_EXCL,0600);if(out<0){close(in);return 0;}
    char buf[8192];unsigned left=file->size;int ok=1;
    while(left){ssize_t n=read(in,buf,left<sizeof(buf)?left:sizeof(buf));if(n<=0){ok=0;break;}
        ssize_t sent=0;while(sent<n){ssize_t w=write(out,buf+sent,n-sent);if(w<=0){ok=0;break;}sent+=w;}if(!ok)break;left-=n;}
    if(read(in,buf,1)!=0)ok=0;
    close(in);if(close(out))ok=0;
    return ok && sd_hash_matches(dest,file->sha256);
}
static int state(char out[96]){
    FILE *f=fopen(SD_RAM "/state","r");if(!f){strcpy(out,"inactive");return 0;}
    if(!fgets(out,96,f))strcpy(out,"cleanup-required");fclose(f);out[strcspn(out,"\r\n")]=0;return 1;
}
static int set_state(const char *s){
    FILE *f=fopen(SD_RAM "/state.part","w");if(!f)return 0;
    int ok=fprintf(f,"%s\n",s)>0;if(fclose(f))ok=0;
    return ok && !rename(SD_RAM "/state.part",SD_RAM "/state");
}
static unsigned long ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned long)t.tv_sec*1000+t.tv_nsec/1000000;}
static int identify(void){
    struct stat st;if(stat(SD_FIRMWARE,&st))return 0;
    struct cached {unsigned magic;struct stat source;char digest[65];} cache;
    FILE *f=fopen(SD_RAM "/firmware-cache","rb");
    if(f){
        int ok=fread(&cache,1,sizeof(cache),f)==sizeof(cache);fclose(f);
        if(ok && cache.magic==0x53444331 && cache.source.st_dev==st.st_dev && cache.source.st_ino==st.st_ino &&
           cache.source.st_size==st.st_size && cache.source.st_mtime==st.st_mtime && cache.digest[64]==0 && sd_hex(cache.digest,64)){
            strcpy(firmware,cache.digest);return 1;
        }
    }
    printf("PICOSD_PHASE identifying-firmware\n");
    if(!sd_sha256(SD_FIRMWARE,firmware))return 0;
    memset(&cache,0,sizeof(cache));cache.magic=0x53444331;cache.source=st;strcpy(cache.digest,firmware);
    f=fopen(SD_RAM "/firmware-cache","wb");if(f){fwrite(&cache,1,sizeof(cache),f);fclose(f);}
    return 1;
}
static int script(const char *entry,unsigned timeout){
    int pipefd[2];if(pipe(pipefd))return 1;
    FILE *log=fopen(SD_RAM "/log","a");if(!log){close(pipefd[0]);close(pipefd[1]);return 1;}
    fseek(log,0,SEEK_END);long logged=ftell(log);if(logged<0)logged=65536;
    pid_t pid=fork();if(pid<0){fclose(log);close(pipefd[0]);close(pipefd[1]);return 1;}
    if(!pid){
        setsid();close(pipefd[0]);dup2(pipefd[1],1);dup2(pipefd[1],2);close(pipefd[1]);
        int null=open("/dev/null",O_RDONLY);if(null>=0){dup2(null,0);close(null);}
        char executable[256];path_join(executable,sizeof(executable),SD_RAM "/active",entry);
        char env_dir[]="MHI2_RUN_DIR=" SD_RAM "/active";
        char *env[]={"PATH=/bin:/usr/bin:/sbin:/usr/sbin:/mnt/app/armle/usr/bin",env_dir,"HOME=" SD_RAM,"TMPDIR=" SD_RAM,NULL};
        char *argv[]={"sh",executable,NULL};
        if(chdir(SD_RAM "/active"))_exit(125);
        execve("/bin/sh",argv,env);_exit(126);
    }
    close(pipefd[1]);fcntl(pipefd[0],F_SETFL,O_NONBLOCK);
    unsigned long began=ms(),reported=began;int status=0,done=0;
    while(!done){
        struct pollfd p={pipefd[0],POLLIN,0};poll(&p,1,20);
        char b[512];ssize_t n;
        for(unsigned drain=0;drain<16 && (n=read(pipefd[0],b,sizeof(b)))>0;drain++)if(logged<65536){size_t keep=n;if(keep>(size_t)(65536-logged))keep=65536-logged;fwrite(b,1,keep,log);logged+=keep;}
        pid_t result=waitpid(pid,&status,WNOHANG);if(result==pid)done=1;
        else if(result<0 || ms()-began>timeout){
            printf("PICOSD_ERROR script-timeout-or-wait-failed\n");
            kill(-pid,SIGKILL);kill(pid,SIGKILL);waitpid(pid,&status,0);status=-1;done=1;
        }
        if(!done && ms()-reported>=5000){
            printf("PICOSD_PHASE %s-script-%lu-seconds\n",!strcmp(entry,"start.sh")?"start":"stop",(ms()-began)/1000);
            reported=ms();
        }
    }
    close(pipefd[0]);fclose(log);return status==-1 || !WIFEXITED(status) || WEXITSTATUS(status)!=0;
}
static int stop(void){
    char s[96];if(!state(s)){printf("PICOSD_STATE inactive\n");return 0;}
    if(!set_state("stopping") || script("stop.sh",SD_STOP_TIMEOUT)){set_state("cleanup-required");return output("stop-failed-reboot-hu-for-recovery");}
    unlink(SD_RAM "/state");unlink(SD_RAM "/active-digest");
    if(!remove_tree(SD_RAM "/active"))return output("ram-cleanup-failed");
    printf("PICOSD_STATE inactive\n");return 0;
}
static int start(const char *mount,const char *digest){
    char s[96];if(state(s))return output("already-started-or-cleanup-required");
    if(!sd_hex(digest,64) || !sd_path(mount) || strncmp(mount,"sd",2))return output("invalid-selection");
    struct card card;if(!read_manifest(mount,&card) || strcmp(card.digest,digest))return output("bundle-changed-or-removed");
    if(strcmp(card.manifest.firmware,"any") && strcmp(card.manifest.firmware,firmware))return output("unsupported-firmware");
    struct statvfs v;if(statvfs(SD_RAM,&v) || (unsigned long long)v.f_bavail*v.f_frsize<card.manifest.bytes+1024u*1024u)return output("insufficient-ram");
    if(!remove_tree(SD_RAM "/staging") || mkdir(SD_RAM "/staging",0700))return output("staging-unavailable");
    for(unsigned i=0;i<card.manifest.count;i++){
        char source[512],dest[512],base[256];struct sd_file *f=&card.manifest.files[i];
        int ok=snprintf(base,sizeof(base),"%s/%s/mhi2",SD_FS,mount)<(int)sizeof(base) &&
            path_join(source,sizeof(source),base,f->path) && path_join(dest,sizeof(dest),SD_RAM "/staging",f->path) &&
            parents(dest) && copy_verified(source,dest,f);
        if(!ok){remove_tree(SD_RAM "/staging");return output("file-missing-changed-or-unsafe");}
    }
    if(!remove_tree(SD_RAM "/active") || rename(SD_RAM "/staging",SD_RAM "/active"))return output("staging-commit-failed");
    FILE *f=fopen(SD_RAM "/active-digest","w");if(!f)return output("state-write-failed");
    int ok=fprintf(f,"%s\n",digest)>0;if(fclose(f))ok=0;
    if(!ok || !set_state("starting"))return output("state-write-failed");
    unlink(SD_RAM "/log");
    unlink(SD_RAM "/progress");
    if(script("start.sh",SD_SCRIPT_TIMEOUT)){int failed=stop();return output(failed?"start-and-cleanup-failed":"start-failed-cleanup-complete");}
    if(!set_state("active"))return output("state-write-failed");
    printf("PICOSD_STATE active\nPICOSD_ACTIVE %s\n",digest);return 0;
}
static void logs(void){
    FILE *f=fopen(SD_RAM "/log","rb");if(!f)return;
    fseek(f,0,SEEK_END);long n=ftell(f);if(n>2048)fseek(f,n-2048,SEEK_SET);else rewind(f);
    unsigned char buf[64];size_t count;
    while((count=fread(buf,1,sizeof(buf),f))){printf("PICOSD_LOG ");for(size_t i=0;i<count;i++)printf("%02x",buf[i]);putchar('\n');}
    fclose(f);
}
static void progress_record(void){
    /* Optional payload telemetry, bounded and token-only. It is data, never
     * a command or a source of controller state. A rename publishes a sample. */
    const char *path=SD_RAM "/progress";struct stat s;
    if(lstat(path,&s)||!S_ISREG(s.st_mode)||s.st_size<=0||s.st_size>=384)return;
    FILE *f=fopen(path,"r");if(!f)return;char b[384];size_t n=fread(b,1,sizeof(b)-1,f);fclose(f);b[n]=0;
    if(!n)return;
    if(b[n-1]=='\n')b[--n]=0;
    for(size_t i=0;i<n;i++)if(!strchr("abcdefghijklmnopqrstuvwxyz0123456789 -",b[i]))return;
    printf("PICOSD_PROGRESS %s\n",b);
}
static int run(int argc,char **argv){
    if(argc<2)return 1;
    setvbuf(stdout,NULL,_IONBF,0);
    printf("PICOSD_PHASE controller-start\n");
    umask(0077);
    if((mkdir(SD_RAM,0700) && errno!=EEXIST) || !directory(SD_RAM))return output("ram-directory-unavailable");
    if(chmod(SD_RAM,0700))return output("ram-permissions-failed");
    int fd=open(SD_RAM "/lock",O_CREAT|O_RDWR,0600);struct flock lock;
    memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    if(fd<0 || fcntl(fd,F_SETLK,&lock)){if(fd>=0)close(fd);return output("busy");}
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    int rc=1;
    if(!strcmp(argv[1],"scan") || !strcmp(argv[1],"run")){
        if(!identify()){output("firmware-identification-failed");goto end;}
        printf("PICOSD_FW %s\n",firmware);
    }
    printf("PICOSD_PHASE %s\n",argv[1]);
    if(!strcmp(argv[1],"scan"))rc=scan();
    else if(!strcmp(argv[1],"run") && argc==4)rc=start(argv[2],argv[3]);
    else if(!strcmp(argv[1],"stop"))rc=stop();
    else if(!strcmp(argv[1],"status"))rc=0;
    else output("invalid-action");
    {char s[96];state(s);printf("PICOSD_STATE %s\n",s);}
    {char d[80];FILE *f=fopen(SD_RAM "/active-digest","r");if(f){if(fgets(d,sizeof(d),f)){d[strcspn(d,"\n")]=0;if(sd_hex(d,64))printf("PICOSD_ACTIVE %s\n",d);}fclose(f);}}
    logs();progress_record();
end:
    close(fd);return rc;
}
#ifdef SD_HOST_TEST
int main(int argc,char **argv){
    if(argc==3 && !strcmp(argv[1],"hash")){char hex[65];if(!sd_sha256(argv[2],hex))return 1;puts(hex);return 0;}
    return run(argc,argv);
}
#else
void mhi2_sd_runner(void){
    const char *a=getenv("PICOSD_ACTION"),*s=getenv("PICOSD_SLOT"),*d=getenv("PICOSD_DIGEST");
    char *argv[]={"sd-runner",(char *)(a?a:"status"),(char *)(s?s:""),(char *)(d?d:""),NULL};
    int rc=run(a && !strcmp(a,"run")?4:2,argv);fflush(stdout);_exit(rc);
}
#endif
