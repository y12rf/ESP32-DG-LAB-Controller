#include "WebAssets.h"

namespace web_assets {

const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>DG-LAB 控制器</title>
  <style>
    :root{color-scheme:dark;font-family:system-ui,-apple-system,sans-serif;background:#0f1217;color:#f5e7a7}
    *{box-sizing:border-box}body{margin:0;background:#0f1217}.app{max-width:720px;margin:auto;padding:16px 16px 84px}
    h1,h2,p{margin-top:0}h1{font-size:22px;margin-bottom:4px}h2{font-size:17px}.card{background:#1a2028;border-radius:16px;padding:16px;margin-bottom:12px}
    .topbar{display:flex;justify-content:space-between;align-items:center}.topbar p{margin-bottom:0;color:#9da9b5}.badge{padding:7px 10px;border-radius:999px;background:#303946;font-size:12px}
    .strength-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.strength-card{text-align:center}.strength-card span{display:block;color:#aab4bf}.strength-card strong{display:block;margin-top:6px;color:#ffe164;font-size:38px}
    .status-list p{display:flex;justify-content:space-between;margin:10px 0}.device-list{display:grid;gap:8px;margin-top:12px}.channel-card h2{display:flex;justify-content:space-between}.channel-card h2 strong{color:#ffe164;font-size:30px}
    button{min-height:44px;border:0;border-radius:11px;padding:10px;background:#286fbd;color:#fff;font-weight:700;cursor:pointer}button:disabled{opacity:.48;cursor:default}
    .step-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.wave-buttons{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:12px}.wave-buttons .active{outline:3px solid #ffe164}
    .device-button,.primary-wide,.danger,.secondary{width:100%;margin-top:8px}.danger{background:#aa4343}.secondary{background:#3b4653}.primary-wide{background:#319653}
    #action-message{min-height:24px;color:#ffdc78}.tabs{position:fixed;left:50%;bottom:8px;transform:translateX(-50%);width:min(calc(100% - 32px),688px);display:grid;grid-template-columns:repeat(3,1fr);gap:6px;background:#11161c;padding:8px;border-radius:14px;box-shadow:0 10px 30px #0008}.tabs .active{background:#ffe164;color:#171a1f}
    #log-list p{padding:9px 0;margin:0;border-bottom:1px solid #303946;color:#d5dbe1;overflow-wrap:anywhere}[hidden]{display:none!important}
    @media(max-width:359px){.strength-grid{grid-template-columns:1fr}.step-grid{grid-template-columns:repeat(2,1fr)}}
  </style>
</head>
<body>
  <main class="app">
    <header class="card topbar">
      <div><h1>DG-LAB</h1><p id="controller-state">正在连接控制器</p></div>
      <span id="ble-state" class="badge">BLE 未连接</span>
    </header>

    <section data-page="status">
      <div id="connected-status" hidden>
        <div class="card"><h2 id="device-name">设备</h2><p id="device-type"></p></div>
        <div class="strength-grid">
          <div class="card strength-card"><span>A 通道</span><strong id="status-a">0</strong></div>
          <div class="card strength-card"><span>B 通道</span><strong id="status-b">0</strong></div>
        </div>
        <div class="card status-list">
          <p>反馈 <b id="feedback-state">未确认</b></p>
          <p>波形 <b id="status-wave">A</b></p>
          <p>输出 <b id="output-state">已停止</b></p>
        </div>
        <button id="disconnect-button" class="danger" type="button">断开设备</button>
      </div>
      <div id="disconnected-status">
        <div class="card"><h2>查找脉冲主机</h2><p>扫描完成后选择一台设备。</p></div>
        <button id="scan-button" type="button">扫描设备</button>
        <button id="auto-connect-button" class="secondary" type="button">自动连接：开启</button>
        <div id="device-list" class="device-list"></div>
      </div>
    </section>

    <section data-page="control" hidden>
      <div id="control-disabled" class="card">连接设备后可用</div>
      <div id="control-content" hidden>
        <div id="channel-a" class="card channel-card"><h2>A 通道 <strong data-value>0</strong></h2><div data-buttons></div></div>
        <div id="channel-b" class="card channel-card"><h2>B 通道 <strong data-value>0</strong></h2><div data-buttons></div></div>
        <div class="card">
          <h2>波形</h2>
          <div class="wave-buttons"><button data-wave="a">A</button><button data-wave="b">B</button><button data-wave="c">C</button></div>
          <button id="output-button" class="primary-wide" type="button">开始输出</button>
        </div>
        <p id="action-message" role="status"></p>
      </div>
    </section>

    <section data-page="logs" hidden>
      <div class="card"><h2>操作日志</h2><div id="log-list"></div></div>
    </section>

    <nav class="tabs" aria-label="页面">
      <button type="button" data-tab="status">状态</button>
      <button type="button" data-tab="control">控制</button>
      <button type="button" data-tab="logs">日志</button>
    </nav>
  </main>
  <script>
  (()=>{'use strict';
  const STATUS_INTERVAL_MS=1000,LOG_INTERVAL_MS=2000;
  const state={status:null,tab:'status',scanRevision:null,statusInFlight:false,logInFlight:false,devicesInFlight:false,actionInFlight:false};
  let statusTimer=0,logTimer=0,requestTail=Promise.resolve();
  const byId=id=>document.getElementById(id);
  const pages=[...document.querySelectorAll('[data-page]')];
  const tabs=[...document.querySelectorAll('[data-tab]')];
  const setText=(id,value)=>{byId(id).textContent=String(value)};
  const form=data=>new URLSearchParams(data);

  function request(path,options={}){
    const perform=async()=>{
      const response=await fetch(path,{cache:'no-store',...options});
      const payload=await response.json();
      if(!response.ok||payload.ok===false)throw new Error(payload.error||'request_failed');
      return payload;
    };
    const pending=requestTail.then(perform,perform);
    requestTail=pending.then(()=>undefined,()=>undefined);
    return pending;
  }

  function errorText(error){
    const names={invalid_argument:'参数无效',device_not_found:'设备已不在列表中',invalid_state:'当前状态不允许此操作',ble_failure:'蓝牙操作失败'};
    return names[error.message]||'与控制器通信失败';
  }

  function renderStatus(s){
    state.status=s;
    setText('controller-state','控制器在线');
    setText('ble-state',s.connected?'BLE 已连接':'BLE 未连接');
    byId('connected-status').hidden=!s.connected;
    byId('disconnected-status').hidden=s.connected;
    byId('control-content').hidden=!s.ready;
    byId('control-disabled').hidden=s.ready;
    if(s.connected){
      setText('device-name',s.name||'已连接设备');
      setText('device-type',s.type===3?'DG-LAB 3.0':'DG-LAB 2.0');
      setText('status-a',s.type===2?Math.floor(s.strengthA/7):s.strengthA);
      setText('status-b',s.type===2?Math.floor(s.strengthB/7):s.strengthB);
      setText('feedback-state',s.waiting?'命令处理中':s.confirmed?'已确认':'待设备确认');
      setText('status-wave',s.wave.toUpperCase());
      setText('output-state',s.sending?'运行中':'已停止');
      document.querySelector('#channel-a [data-value]').textContent=s.type===2?Math.floor(s.strengthA/7):s.strengthA;
      document.querySelector('#channel-b [data-value]').textContent=s.type===2?Math.floor(s.strengthB/7):s.strengthB;
      byId('output-button').textContent=s.sending?'停止输出':'开始输出';
      document.querySelectorAll('[data-wave]').forEach(button=>button.classList.toggle('active',button.dataset.wave===s.wave));
    }
    byId('auto-connect-button').textContent='自动连接：'+(s.autoConnect?'开启':'关闭');
    if(!s.connected&&state.scanRevision!==s.scanRevision){
      state.scanRevision=s.scanRevision;
      refreshDevices();
    }
  }

  async function refreshStatus(){
    if(document.hidden||state.statusInFlight||state.actionInFlight)return;
    state.statusInFlight=true;
    try{renderStatus(await request('/api/status'))}
    catch(error){setText('controller-state','与控制器通信中断')}
    finally{state.statusInFlight=false}
  }

  async function refreshDevices(){
    if(document.hidden||state.tab!=='status'||state.status?.connected||state.devicesInFlight)return;
    state.devicesInFlight=true;
    try{
      const payload=await request('/api/devices');
      const list=byId('device-list');list.replaceChildren();
      payload.devices.forEach(device=>{
        const button=document.createElement('button');
        button.type='button';button.className='device-button';
        button.textContent=`${device.name} · ${device.type}.0 · ${device.rssi} dBm`;
        button.addEventListener('click',()=>postAction('/api/connect',{address:device.address,type:device.type},button));
        list.appendChild(button);
      });
    }catch(error){setText('controller-state',errorText(error))}
    finally{state.devicesInFlight=false}
  }

  async function refreshLogs(){
    if(document.hidden||state.tab!=='logs'||state.logInFlight)return;
    state.logInFlight=true;
    try{
      const payload=await request('/api/logs');
      const list=byId('log-list');list.replaceChildren();
      payload.logs.forEach(text=>{const row=document.createElement('p');row.textContent=text;list.appendChild(row)});
    }catch(error){setText('controller-state',errorText(error))}
    finally{state.logInFlight=false}
  }

  async function postAction(path,data,button){
    if(state.actionInFlight)return;
    state.actionInFlight=true;if(button)button.disabled=true;
    try{
      const result=await request(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form(data)});
      if(result.disposition)setText('action-message',result.disposition==='queued'?'强度命令已排队':'强度命令待发送');
    }catch(error){setText('action-message',errorText(error))}
    finally{
      state.actionInFlight=false;if(button)button.disabled=false;
      await refreshStatus();
    }
  }

  function switchTab(tab){
    state.tab=tab;
    pages.forEach(page=>page.hidden=page.dataset.page!==tab);
    tabs.forEach(button=>button.classList.toggle('active',button.dataset.tab===tab));
    stopLogTimer();
    if(tab==='logs'){refreshLogs();logTimer=setInterval(refreshLogs,LOG_INTERVAL_MS)}
    else if(state.status&&!state.status.connected)refreshDevices();
  }
  function stopStatusTimer(){if(statusTimer){clearInterval(statusTimer);statusTimer=0}}
  function stopLogTimer(){if(logTimer){clearInterval(logTimer);logTimer=0}}
  function startStatusTimer(){stopStatusTimer();if(!document.hidden)statusTimer=setInterval(refreshStatus,STATUS_INTERVAL_MS)}

  function addStrengthButtons(root,channel){
    const values=[[-10,'-10'],[-5,'-5'],[-1,'-1'],[1,'+1'],[5,'+5'],[10,'+10']];
    const grid=document.createElement('div');grid.className='step-grid';
    values.forEach(([value,label])=>{
      const button=document.createElement('button');button.type='button';button.textContent=label;
      const method=channel==='a'?(value>0?4:8):(value>0?1:2);
      button.addEventListener('click',()=>postAction('/api/strength',{channel,value:Math.abs(value),method},button));grid.appendChild(button);
    });
    [['归零',0],['50%',50]].forEach(([label,kind])=>{
      const button=document.createElement('button');button.type='button';button.textContent=label;
      button.addEventListener('click',()=>{
        const type=state.status?.type||3,value=kind===0?0:(type===3?100:146),method=channel==='a'?12:3;
        postAction('/api/strength',{channel,value,method},button);
      });grid.appendChild(button);
    });
    root.replaceChildren(grid);
  }

  tabs.forEach(button=>button.addEventListener('click',()=>switchTab(button.dataset.tab)));
  addStrengthButtons(document.querySelector('#channel-a [data-buttons]'),'a');
  addStrengthButtons(document.querySelector('#channel-b [data-buttons]'),'b');
  byId('scan-button').addEventListener('click',async event=>{
    const button=event.currentTarget,previous=button.textContent;
    button.textContent='扫描中';
    await postAction('/api/scan',{},button);
    button.textContent=previous;
  });
  byId('disconnect-button').addEventListener('click',event=>postAction('/api/disconnect',{},event.currentTarget));
  byId('auto-connect-button').addEventListener('click',event=>postAction('/api/auto-connect',{enabled:state.status?.autoConnect?0:1},event.currentTarget));
  byId('output-button').addEventListener('click',event=>postAction('/api/output',{sending:state.status?.sending?0:1},event.currentTarget));
  document.querySelectorAll('[data-wave]').forEach(button=>button.addEventListener('click',()=>postAction('/api/wave',{type:button.dataset.wave},button)));
  document.addEventListener('visibilitychange',()=>{
    if(document.hidden){stopStatusTimer();stopLogTimer();return}
    refreshStatus();startStatusTimer();if(state.tab==='logs'){refreshLogs();logTimer=setInterval(refreshLogs,LOG_INTERVAL_MS)}
  });
  switchTab('status');refreshStatus();startStatusTimer();
  })();
  </script>
</body>
</html>)HTML";

}  // namespace web_assets
