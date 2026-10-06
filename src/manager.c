/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "manager.h"
#include "usb_asix.h"
#include "lwip/tcp.h"
#include "pico/time.h"
#include "manager_asset.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static struct tcp_pcb *connection;
static ip_addr_t address;
static char username[65],password[65],action[16],slot[48],digest[65];
static char line[1024],pending[2048],fw[65],hu_state[40]="unknown",active[65];
static char log_text[2049];static size_t log_used;
struct card {char slot[48],digest[65],name[49];bool compatible;};
static struct card cards[8];static unsigned count;
static size_t used,queued;
static unsigned phase,telnet,verb,suboption,subcommand,sequence;
static uint32_t started,operation_started,next_scan,next_progress,sampled_at,advanced_at;
static bool wanted,done,up,authenticated,manual,autorun,armed,stop_pending,progress_seen,executing;
static struct {
    bool valid;char phase[24],source[24];
    unsigned long long copied,checked,verified,total,part,part_done,part_size,elapsed_ms,sequence;
} progress;
static char trusted_slot[48],trusted_digest[65];
static const char *status="Enter HU credentials to connect";
static char result_line[64];
static char phase_status[96];
static uint32_t now(void){return to_ms_since_boot(get_absolute_time());}
static bool hex(const char *s,unsigned n){if(strlen(s)!=n)return false;for(unsigned i=0;i<n;i++)if(!strchr("0123456789abcdef",s[i]))return false;return true;}
static bool word(const char *s,size_t limit){size_t n=strlen(s);if(!n || n>=limit)return false;for(size_t i=0;i<n;i++)if(!((s[i]>='a' && s[i]<='z') || (s[i]>='A' && s[i]<='Z') || (s[i]>='0' && s[i]<='9') || strchr("._-",s[i])))return false;return strcmp(s,".") && strcmp(s,"..");}
static void close_connection(void){
    if(connection){struct tcp_pcb *p=connection;connection=NULL;tcp_arg(p,NULL);tcp_recv(p,NULL);tcp_err(p,NULL);tcp_abort(p);}
    memset(pending,0,sizeof(pending));queued=used=0;wanted=false;executing=false;
}
static void fail(const char *reason){authenticated=false;status=reason;done=true;close_connection();autorun=false;stop_pending=false;strcpy(hu_state,"unknown");}
static void queue(const void *p,size_t n){if(n>sizeof(pending)-queued){fail("Protocol overflow; reconnect manually");return;}memcpy(pending+queued,p,n);queued+=n;}
static void send_line(const char *p){queue(p,strlen(p));queue("\r\n",2);}
static void pump(void){
    if(!connection || !queued)return;
    unsigned n=queued<tcp_sndbuf(connection)?queued:tcp_sndbuf(connection);
    if(n && tcp_write(connection,pending,n,TCP_WRITE_FLAG_COPY)==ERR_OK){memmove(pending,pending+n,queued-n);memset(pending+queued-n,0,n);queued-=n;tcp_output(connection);}
}
static void command(void){
    char cmd[1024];
    /* Only fixed verbs and validated tokens enter this command. Credentials
     * are supplied at Telnet prompts, never interpolated into a shell. */
    snprintf(result_line,sizeof(result_line),"PICOSD_DONE_%u_",++sequence);
    int n=snprintf(cmd,sizeof(cmd),
        "umask 077; w=/ramdisk/pico-manager; mkdir -p $w && "
        "{ test -f $w/runner-%s.so || { /mnt/app/armle/usr/bin/ftp -o $w/runner.part http://172.16.250.1%s && mv $w/runner.part $w/runner-%s.so; }; } && "
        "PICOSD_ACTION=%s PICOSD_SLOT=%s PICOSD_DIGEST=%s LD_PRELOAD=$w/runner-%s.so /bin/ls; "
        "pico_sd_rc=$?; pico_sd_mark=%s; echo ${pico_sd_mark}${pico_sd_rc}",
        MANAGER_RUNNER_ID,MANAGER_RUNNER_PATH,MANAGER_RUNNER_ID,action,slot,digest,MANAGER_RUNNER_ID,result_line);
    if(n<0 || n>=(int)sizeof(cmd)){fail("Command overflow");return;}
    send_line(cmd);executing=true;status="Authenticated; running HU controller";operation_started=started=now();
}
static void record(void){
    if(!strncmp(line,"PICOSD_PHASE ",13) && word(line+13,64)){
        snprintf(phase_status,sizeof(phase_status),"HU controller: %.63s",line+13);status=phase_status;started=now();
    }
    else if(!strncmp(line,"PICOSD_FW ",10) && hex(line+10,64))strcpy(fw,line+10);
    else if(!strncmp(line,"PICOSD_STATE ",13) && word(line+13,sizeof(hu_state)))strcpy(hu_state,line+13);
    else if(!strncmp(line,"PICOSD_ACTIVE ",14) && hex(line+14,64))strcpy(active,line+14);
    else if(!strncmp(line,"PICOSD_PROGRESS v1 ",19)){
        char phase_name[24],source[24],extra;
        unsigned long long copied,checked,verified,total,part,part_done,part_size,elapsed,seq;
        if(sscanf(line+19,"%23s %23s %llu %llu %llu %llu %llu %llu %llu %llu %llu %c",
            phase_name,source,&copied,&checked,&verified,&total,&part,&part_done,&part_size,&elapsed,&seq,&extra)==11 &&
            (!strcmp(phase_name,"preparing")||!strcmp(phase_name,"copying")||!strcmp(phase_name,"verifying")||
             !strcmp(phase_name,"complete")||!strcmp(phase_name,"failed")||!strcmp(phase_name,"cancelled")||!strcmp(phase_name,"cleanup-required")) &&
            (!strcmp(source,"none")||!strcmp(source,"mmx-emmc")||!strcmp(source,"mmx-nor")) &&
            total<=(UINT64_C(2)<<40) && copied<=total && checked<=copied && verified<=checked &&
            part<=8192 && part_done<=part_size && part_size<=UINT64_C(4294966784) &&
            elapsed<=UINT64_C(31536000000) && seq<=UINT64_C(31536000) &&
            (strcmp(phase_name,"complete") || (total && verified==total))){
            if(!progress.valid || seq!=progress.sequence)advanced_at=now();
            sampled_at=now();progress_seen=progress.valid=true;
            strcpy(progress.phase,phase_name);strcpy(progress.source,source);
            progress.copied=copied;progress.checked=checked;progress.verified=verified;progress.total=total;
            progress.part=part;progress.part_done=part_done;progress.part_size=part_size;progress.elapsed_ms=elapsed;progress.sequence=seq;
        }
    }
    else if(!strncmp(line,"PICOSD_LOG ",11)){
        const char *s=line+11;size_t n=strlen(s);
        if(n<=128 && !(n%2) && hex(s,n))for(size_t i=0;i<n && log_used<sizeof(log_text)-1;i+=2){
            char pair[3]={s[i],s[i+1],0};unsigned c=strtoul(pair,NULL,16);
            log_text[log_used++]=(c=='\n' || c=='\t' || (c>=32 && c<=126))?c:'.';
        }
        log_text[log_used]=0;
    }else if(!strncmp(line,"PICOSD_CARD ",12) && count<8){
        struct card c;char compatible[20],extra;
        if(sscanf(line+12,"%47s %64s %48s %19s %c",c.slot,c.digest,c.name,compatible,&extra)==4 &&
           word(c.slot,sizeof(c.slot)) && !strncmp(c.slot,"sd",2) && hex(c.digest,64) && word(c.name,sizeof(c.name)) &&
           (!strcmp(compatible,"compatible") || !strcmp(compatible,"unsupported"))){
            c.compatible=!strcmp(compatible,"compatible");cards[count++]=c;
        }
    }else if(!strncmp(line,result_line,strlen(result_line))){
        if(!progress_seen)progress.valid=false;
        if(!strcmp(line+strlen(result_line),"0")){
            status="HU request completed";done=true;
            if(!strcmp(action,"scan") && autorun){
                bool found=false;for(unsigned i=0;i<count;i++)if(!strcmp(cards[i].slot,trusted_slot) && !strcmp(cards[i].digest,trusted_digest))found=true;
                if(!found)armed=true;
            }
        }else {status="HU request failed; inspect log and rescan";done=true;autorun=false;}
    }
    /* Error strings are fixed runner codes, restricted before displaying. */
    if(!strncmp(line,"PICOSD_ERROR ",13) && word(line+13,96)){
        size_t n=strlen(line+13);if(log_used+n+1<sizeof(log_text)){memcpy(log_text+log_used,line+13,n);log_used+=n;log_text[log_used++]='\n';log_text[log_used]=0;}
    }
}
static void visible(unsigned char b){
    if(b=='\r' || !b)return;
    if(b=='\n'){line[used]=0;if(phase==3)record();used=0;return;}
    if(used+1>=sizeof(line)){fail("Oversized service response");return;}
    line[used++]=b;line[used]=0;
    if(!phase && used>=6 && !strcmp(line+used-6,"login:")){phase=1;used=0;send_line(username);status="Stock login in progress";}
    else if(phase==1 && used>=9 && !strcmp(line+used-9,"Password:")){phase=2;used=0;send_line(password);}
    else if(phase==2 && strstr(line,"Login incorrect")){authenticated=false;memset(password,0,sizeof(password));fail("HU authentication rejected; re-enter credentials");}
    else if(phase==2 && used>=2 && !strcmp(line+used-2,"# ")){authenticated=true;phase=3;used=0;command();}
}
static void byte(unsigned char b){
    switch(telnet){
    case 0:if(b==255)telnet=1;else visible(b);break;
    case 1:if(b==255){visible(b);telnet=0;}else if(b>=251 && b<=254){verb=b;telnet=2;}else if(b==250){suboption=subcommand=0;telnet=3;}else telnet=0;break;
    case 2:{unsigned char reply[3]={255,0,b};if(verb==251)reply[1]=(b==1 || b==3)?253:254;if(verb==253)reply[1]=(b==3 || b==24)?251:252;if(reply[1])queue(reply,3);telnet=0;break;}
    case 3:suboption=b;telnet=4;break;
    case 4:subcommand=b;telnet=5;break;
    case 5:if(b==255)telnet=6;break;
    case 6:if(b==240){if(suboption==24 && subcommand==1){static const unsigned char t[]={255,250,24,0,'V','T','1','0','0',255,240};queue(t,sizeof(t));}telnet=0;}else telnet=5;break;
    }
}
static err_t receive(void *arg,struct tcp_pcb *p,struct pbuf *b,err_t error){
    (void)arg;(void)error;
    if(!b){fail("HU disconnected; reconnect manually");return ERR_ABRT;}
    unsigned n=b->tot_len;
    for(struct pbuf *part=b;part;part=part->next)for(unsigned i=0;i<part->len && connection && !done;i++)byte(((unsigned char *)part->payload)[i]);
    if(connection)tcp_recved(p,n);
    pbuf_free(b);
    if(!connection)return ERR_ABRT;
    if(done){
        wanted=false;executing=false;next_progress=now()+2000;
        if(strcmp(action,"status"))next_scan=now()+10000;
        /* Keep one authenticated shell while a payload is active. Status
         * polling does not repeatedly authenticate or rehash SD contents. */
        if(strcmp(hu_state,"active") || !authenticated){close_connection();return ERR_ABRT;}
    }
    pump();return ERR_OK;
}
static void error_callback(void *arg,err_t error){(void)arg;(void)error;connection=NULL;fail("Service lost; reconnect manually");}
static err_t connected(void *arg,struct tcp_pcb *p,err_t error){(void)arg;(void)p;(void)error;status="Waiting for stock login prompt";return ERR_OK;}
void manager_init(const ip_addr_t *hu){
    address=*hu;
}
bool manager_busy(void){return executing || wanted;}
const char *manager_status(void){return status;}
bool manager_request(const char *a,const char *u,const char *p,const char *s,const char *d){
    if(!strcmp(a,"disconnect")){
        close_connection();memset(username,0,sizeof(username));memset(password,0,sizeof(password));authenticated=false;autorun=false;stop_pending=false;
        status="Credentials cleared; HU payload may still be active";
        return true;
    }
    if(!strcmp(a,"auto-off")){autorun=false;return true;}
    if(!strcmp(a,"stop") && manager_busy() && !strcmp(action,"status") && authenticated){
        stop_pending=true;autorun=false;return true;
    }
    if(manager_busy() || manual || !up)return false;
    if(!strcmp(a,"connect")){
        if(*u || *p){
            if(!word(u,sizeof(username)) || !*p || strlen(p)>=sizeof(password))return false;
            for(const char *c=p;*c;c++)if(*c<32 || *c>126)return false;
            strcpy(username,u);strcpy(password,p);
        }else if(!username[0] || !password[0])return false;
        close_connection();a="scan";authenticated=false;
    }else if(!username[0] || !password[0])return false;
    if(!strcmp(a,"auto-on") || !strcmp(a,"run")){
        if(!word(s,sizeof(slot)) || !hex(d,64))return false;
        bool found=false;for(unsigned i=0;i<count;i++)if(!strcmp(cards[i].slot,s) && !strcmp(cards[i].digest,d) && cards[i].compatible)found=true;
        if(!found)return false;
        if(!strcmp(a,"auto-on")){strcpy(trusted_slot,s);strcpy(trusted_digest,d);autorun=true;armed=true;next_scan=now();return true;}
        strcpy(slot,s);strcpy(digest,d);armed=false;
    }else if(strcmp(a,"scan") && strcmp(a,"stop") && strcmp(a,"status"))return false;
    else {slot[0]=digest[0]=0;}
    if(!strcmp(a,"stop"))autorun=false;
    if(!strcmp(a,"run"))progress.valid=false;
    progress_seen=false;
    strcpy(action,a);wanted=true;done=false;status="Request queued";
    if(!strcmp(a,"scan"))count=0;
    log_used=0;log_text[0]=0;active[0]=0;return true;
}
void manager_poll(bool usb_up,bool manual_session){
    manual=manual_session;
    if(!usb_up){if(up){close_connection();authenticated=false;stop_pending=false;strcpy(hu_state,"unknown");status="USB disconnected; HU state unknown";armed=true;}up=false;return;}
    up=true;
    if(manual){autorun=false;stop_pending=false;if(connection)fail("Manual service session interrupted request");return;}
    if(connection && executing){
        pump();if(now()-started>(phase==3?90000u:20000u) || (phase==3 && now()-operation_started>300000u))fail("HU request timed out; inspect HU state before retry");return;
    }
    if(!wanted && stop_pending){stop_pending=false;manager_request("stop","","","","");}
    if(!wanted && autorun && (int32_t)(now()-next_scan)>=0){
        if(armed && count==1 && cards[0].compatible && !strcmp(cards[0].slot,trusted_slot) && !strcmp(cards[0].digest,trusted_digest) && !strcmp(hu_state,"inactive")){
            manager_request("run","","",trusted_slot,trusted_digest);armed=false;
        }else manager_request("scan","","","","");
        next_scan=now()+10000;
    }
    if(!wanted && authenticated && !strcmp(hu_state,"active") && (int32_t)(now()-next_progress)>=0)
        manager_request("status","","","","");
    if(!wanted)return;
    if(connection && phase==3 && authenticated){used=0;command();pump();return;}
    connection=tcp_new();if(!connection){fail("No TCP connection available");return;}
    used=queued=phase=telnet=0;done=false;executing=true;operation_started=0;started=now();
    tcp_recv(connection,receive);tcp_err(connection,error_callback);tcp_nagle_disable(connection);
    if(tcp_bind(connection,netif_ip_addr4(&usb_netif),0)!=ERR_OK || tcp_connect(connection,&address,23,connected)!=ERR_OK)fail("Service connection failed");
}
static size_t escaped(char *out,size_t size,const char *s){
    size_t n=0;for(;*s;s++){
        unsigned char c=*s;const char *e=c=='\n'?"\\n":c=='\t'?"\\t":c=='"'?"\\\"":c=='\\'?"\\\\":NULL;
        size_t len=e?2:1;if(n+len>=size)break;if(e){out[n++]=e[0];out[n++]=e[1];}else out[n++]=c;
    }out[n]=0;return n;
}
size_t manager_json(char *out,size_t size){
    int n=snprintf(out,size,"{\"busy\":%s,\"authenticated\":%s,\"credentials_ready\":%s,\"autorun\":%s,\"status\":\"%s\",\"hu_state\":\"%s\",\"firmware_sha256\":\"%s\",\"active_digest\":\"%s\",\"cards\":[",
        manager_busy()?"true":"false",authenticated?"true":"false",username[0] && password[0]?"true":"false",autorun?"true":"false",status,hu_state,fw,active);
    if(n<0 || (size_t)n>=size)return 0;
    size_t used=n;
    for(unsigned i=0;i<count;i++){
        n=snprintf(out+used,size-used,"%s{\"slot\":\"%s\",\"digest\":\"%s\",\"name\":\"%s\",\"compatible\":%s}",i?",":"",cards[i].slot,cards[i].digest,cards[i].name,cards[i].compatible?"true":"false");
        if(n<0 || (size_t)n>=size-used)return 0;
        used+=n;
    }
    n=snprintf(out+used,size-used,"],\"live_connected\":%s,\"stop_pending\":%s,\"progress\":",
        connection && authenticated?"true":"false",stop_pending?"true":"false");
    if(n<0 || (size_t)n>=size-used)return 0;
    used+=n;
    if(progress.valid)n=snprintf(out+used,size-used,
        "{\"phase\":\"%s\",\"source\":\"%s\",\"copied\":%llu,\"checked\":%llu,\"verified\":%llu,\"total\":%llu,\"part\":%llu,\"part_done\":%llu,\"part_size\":%llu,\"elapsed_ms\":%llu,\"sequence\":%llu,\"sample_age_ms\":%lu,\"heartbeat_age_ms\":%lu}",
        progress.phase,progress.source,progress.copied,progress.checked,progress.verified,progress.total,progress.part,progress.part_done,progress.part_size,progress.elapsed_ms,progress.sequence,(unsigned long)(now()-sampled_at),(unsigned long)(now()-advanced_at));
    else n=snprintf(out+used,size-used,"null");
    if(n<0 || (size_t)n>=size-used)return 0;
    used+=n;
    if(size-used<16)return 0;
    memcpy(out+used,",\"log\":\"",8);used+=8;
    used+=escaped(out+used,size-used-3,log_text);memcpy(out+used,"\"}",3);return used+2;
}
