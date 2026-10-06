'use strict';
const $=id=>document.getElementById(id);
let cards=[],manager={},cursor=0,session=0,consoleWatching=false,consolePending=false,tab='overview',settingsLoaded=false;
const text=(id,value)=>{$(id).textContent=value;};
function notice(message){text('notice',message);$('notice').hidden=!message;}
async function request(path,headers={},method='GET'){
  const response=await fetch(path,{method,headers,cache:'no-store'});
  if(!response.ok)throw Error((await response.text()).trim()||`Request failed (${response.status})`);
  return response;
}
const json=async(path,headers={})=>(await request(path,headers)).json();
async function action(name,headers={}){
  await request('/manage/action',{'X-MHI2-Action':name,...headers},'POST');notice('');await refresh();
}
function guarded(fn){return async event=>{if(event)event.preventDefault();try{await fn(event);}catch(error){notice(error.message);}};}
function selected(){return cards.find(c=>c.digest===$('bundle').value);}
function selection(){const c=selected();text('bundle-detail',c?`${c.slot} · SHA-256 ${c.digest.slice(0,16)}… · ${c.compatible?'Compatible':'Firmware mismatch'}`:'No compatible SD bundle selected.');$('run').disabled=!c||!c.compatible||manager.busy||!manager.authenticated;}
function human(n){return `${(n/1073741824).toFixed(2)} GiB`;}
function renderProgress(p){
  $('progress-area').hidden=!p;if(!p)return;
  const done=p.phase==='complete',total=Number(p.total),fraction=total>0?(Number(p.copied)+Number(p.verified))/(total*2):0;
  const percent=done?100:Math.min(99.9,Math.max(0,fraction*100));
  $('progress').value=percent;text('percent',`${percent.toFixed(1)}%`);text('progress-title',`${p.phase} · ${p.source}`);
  text('progress-detail',`Copied ${human(p.copied)} / ${human(total)} · Verified ${human(p.verified)} · Part ${p.part} · ${Math.floor(p.elapsed_ms/60000)} min elapsed${p.heartbeat_age_ms>15000?' · Waiting for fresh progress…':''}`);
}
async function refresh(){
  try{
    const [device,m]=await Promise.all([json('/status'),json('/manage/status')]);manager=m;
    text('connection',device.usb_network_up?'USB connected':'Waiting for USB');$('connection').classList.toggle('good',device.usb_network_up);
    text('usb-state',device.usb_network_up?'Link up':'Not connected');text('manager-status',m.status);
    text('hu-state',m.hu_state);text('live-state',m.live_connected?'Connected':'Disconnected');text('phase',m.progress?m.progress.phase:m.hu_state);
    text('log',m.log||'Your payload’s output will appear here.');$('autorun').checked=m.autorun;
    const previous=$('bundle').value;cards=m.cards||[];$('bundle').replaceChildren();
    if(!cards.length){const o=document.createElement('option');o.textContent=m.authenticated?'No SD bundles found':'Connect to discover SD cards';o.value='';$('bundle').append(o);}
    for(const c of cards){const o=document.createElement('option');o.value=c.digest;o.textContent=`${c.name} · ${c.slot}`;$('bundle').append(o);}
    if(cards.some(c=>c.digest===previous))$('bundle').value=previous;
    selection();renderProgress(m.progress);
  }catch(error){text('connection','Pico unreachable');$('connection').classList.remove('good');text('manager-status','Reconnect to MST-Link Wi-Fi to refresh status.');}
}
async function loadSettings(){
  const s=await json('/api/settings');$('ssid').value=s.ssid;$('forwards').replaceChildren();
  s.forwards.forEach(([local,remote],i)=>{
    const row=document.createElement('div');row.className='port-row';
    for(const [kind,value] of [['local',local],['remote',remote]]){
      if(kind==='remote'){const arrow=document.createElement('span');arrow.textContent='→';row.append(arrow);}
      const input=document.createElement('input');input.type='number';input.min='0';input.max='65535';input.required=true;input.value=value;input.id=`${kind}-${i}`;input.setAttribute('aria-label',`${kind==='local'?'Pico':'Head-unit'} port ${i+1}`);row.append(input);
    }$('forwards').append(row);
  });settingsLoaded=true;
}
for(const button of document.querySelectorAll('[data-tab]'))button.onclick=guarded(async()=>{
  tab=button.dataset.tab;for(const panel of document.querySelectorAll('.tab'))panel.hidden=panel.id!==tab;
  for(const b of document.querySelectorAll('[data-tab]')){b.classList.toggle('selected',b===button);if(b===button)b.setAttribute('aria-current','page');else b.removeAttribute('aria-current');}
  if(tab==='settings'&&!settingsLoaded)await loadSettings();
  if(tab==='console')await pollConsole();
});
$('login').onsubmit=guarded(async()=>{const user=$('username').value,password=$('password').value;$('password').value='';await action('connect',{'X-HU-User':user,'X-HU-Password':password});});
$('forget').onclick=guarded(()=>action('disconnect'));
$('scan').onclick=guarded(()=>action('scan'));
$('bundle').onchange=selection;
$('run').onclick=guarded(async()=>{const c=selected();if(!c)throw Error('Select a bundle first.');await action('run',{'X-MHI2-Slot':c.slot,'X-MHI2-Digest':c.digest});});
$('stop').onclick=guarded(()=>action('stop'));
$('autorun').onchange=guarded(async()=>{const c=selected();if($('autorun').checked&&!c)throw Error('Select a bundle first.');await action($('autorun').checked?'auto-on':'auto-off',c?{'X-MHI2-Slot':c.slot,'X-MHI2-Digest':c.digest}:{});});
$('settings-form').onsubmit=guarded(async()=>{
  const headers={'X-MST-SSID':$('ssid').value,'X-MST-Password':$('wifi-password').value};
  for(let i=0;i<3;i++)headers[`X-MST-Forward${i+1}`]=`${$(`local-${i}`).value}:${$(`remote-${i}`).value}`;
  await request('/api/settings',headers,'POST');$('wifi-password').value='';notice('Settings saved. The Pico is restarting. Reconnect to your Wi-Fi network, then reload this page.');settingsLoaded=false;
});
async function consoleAction(action,data=''){
  const hex=Array.from(new TextEncoder().encode(data),b=>b.toString(16).padStart(2,'0')).join('');
  await request('/api/console',{'X-MST-Action':action,'X-MST-Session':String(session),'X-MST-Data':hex},'POST');
}
// Safe text output, not HTML. Stateful escape filtering survives poll boundaries.
let escapeState=0;
function terminalText(bytes){
  let out='';for(const c of bytes){
    if(escapeState===1){escapeState=c===91?2:c===93?3:0;continue;}
    if(escapeState===2){if(c>=64&&c<=126)escapeState=0;continue;}
    if(escapeState===3){if(c===7)escapeState=0;else if(c===27)escapeState=4;continue;}
    if(escapeState===4){escapeState=c===92?0:3;continue;}
    if(c===27){escapeState=1;continue;}
    if(c===8||c===127){out=out.slice(0,-1);continue;}
    if(c===10||c===9||c>=32&&c<127)out+=String.fromCharCode(c);
  }return out;
}
function appendTerminal(value){const el=$('terminal');const bottom=el.scrollTop+el.clientHeight>=el.scrollHeight-40;el.textContent=(el.textContent+value).slice(-32768);if(bottom)el.scrollTop=el.scrollHeight;}
async function pollConsole(){
  if(consolePending)return;consolePending=true;
  try{
    let s=await json('/api/console',{'X-MST-Cursor':String(cursor)});
    if(session!==s.session){session=s.session;cursor=0;escapeState=0;$('terminal').textContent='';s=await json('/api/console',{'X-MST-Cursor':'0'});}
    if(s.lost)appendTerminal('\n[Older output expired from the Pico buffer]\n');
    appendTerminal(terminalText(Array.from(s.data.match(/../g)||[],h=>parseInt(h,16))));cursor=s.next;
    text('console-state',s.status);$('console-state').classList.toggle('good',s.ready);consoleWatching=s.connected;
    $('console-open').disabled=s.connected;$('console-close').disabled=!s.connected;
  }finally{consolePending=false;}
}
$('console-open').onclick=guarded(async()=>{await consoleAction('open');cursor=0;escapeState=0;await pollConsole();notice('Console opened. SD management is paused until you reconnect it.');});
$('console-close').onclick=guarded(async()=>{await consoleAction('close');await pollConsole();});
$('console-clear').onclick=()=>{$('terminal').textContent='';};
$('secret-input').onchange=()=>{$('command').type=$('secret-input').checked?'password':'text';};
$('console-input').onsubmit=guarded(async()=>{const value=$('command').value;await consoleAction('send',value+'\r\n');$('command').value='';await pollConsole();});
$('interrupt').onclick=guarded(()=>consoleAction('send','\x03'));
refresh();setInterval(refresh,2500);setInterval(()=>{if(tab==='console'||consoleWatching)pollConsole().catch(error=>text('console-state',error.message));},350);
