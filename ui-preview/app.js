const paths={phone:'M7 2h10a2 2 0 0 1 2 2v16a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2M10 5h4M11 19h2',power:'M12 2v9M7 5a9 9 0 1 0 10 0',counter:'M6 3h12v18H6zM9 7h6M9 12h6M12 9v6M9 18h6',timer:'M8 2h8M12 2v3M12 5a8 8 0 1 0 0 16 8 8 0 0 0 0-16M12 8v5l3 2',focus:'M12 3v3M8 4l2 2M15 4l-3 2M5 8c-5 8 2 15 7 13 5 2 12-5 7-13-2-3-5-2-7-1-2-1-5-2-7 1',level:'M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20M4 12h16M12 5v3M12 16v3m-5-7 3-3m4 6 3-3',weather:'M8 3v2M3 8h2M4 4l2 2M8 7a3 3 0 0 1 3 3M5 17h13a4 4 0 0 0-1-8 5 5 0 0 0-9 3 3 3 0 0 0-3 5M8 20h.01M13 20h.01M18 20h.01',camera:'M3 7h4l2-3h6l2 3h4v14H3zM16 14a4 4 0 1 0-8 0 4 4 0 0 0 8 0',wifi:'M3 9a14 14 0 0 1 18 0M6 13a9 9 0 0 1 12 0M9 17a4 4 0 0 1 6 0M12 21h.01',ble:'m8 6 9 12-5 4V2l5 4L8 18',sun:'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8M12 2v2m0 16v2M2 12h2m16 0h2M5 5l1 1m12 12 1 1M5 19l1-1M18 6l1-1',sound:'M3 9h4l5-4v14l-5-4H3zM16 8a6 6 0 0 1 0 8M19 5a10 10 0 0 1 0 14',battery:'M3 7h16v10H3zM22 10v4M6 10h10v4H6z',settings:'M10.21 4.82L10.35 2.644L13.65 2.644L13.79 4.82L15.811 5.657L17.449 4.218L19.782 6.551L18.343 8.189L19.18 10.21L21.356 10.35L21.356 13.65L19.18 13.79L18.343 15.811L19.782 17.449L17.449 19.782L15.811 18.343L13.79 19.18L13.65 21.356L10.35 21.356L10.21 19.18L8.189 18.343L6.551 19.782L4.218 17.449L5.657 15.811L4.82 13.79L2.644 13.65L2.644 10.35L4.82 10.21L5.657 8.189L4.218 6.551L6.551 4.218L8.189 5.657zM15.1 12a3.1 3.1 0 1 0-6.2 0 3.1 3.1 0 1 0 6.2 0z',stop:'M9 2h6M12 2v3M18 5l2 2M12 6a8 8 0 1 0 0 16 8 8 0 0 0 0-16M12 10v5l3 2',box:'M3 8h18v13H3zM8 8V4h8v4M3 13h18M10 12h4v4h-4z',back:'m15 5-7 7 7 7',up:'m6 15 6-6 6 6',right:'m9 5 7 7-7 7',clock:'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18M12 7v6h5'};
const icon=name=>name === 'battery' ? '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="2" y="6" width="18" height="12" rx="1.5"/><path d="M22 10v4"/><rect x="5" y="9" width="10" height="6" fill="currentColor" stroke="none"/></svg>' : `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${paths[name]||paths.box}"/></svg>`;
const pages={boot:'启动',home:'数字表盘',analog:'指针表盘',faces:'表盘样式',drawer:'应用抽屉',clock:'数字时钟浮层',control:'控制中心',wifi:'Wi-Fi 设置',ap:'AP 配网',settings:'系统设置',stopwatch:'秒表',phone:'手机配网'};
let wifi=true,ble=false,appIndex=0,running=false,elapsed=0,startTime=0;
let selectedFace = 'home';
const apps=[['秒表','stop','stopwatch'],['设置','settings','settings'],['时钟','clock','home']];
const back=()=>`<button class="back" data-go="home" aria-label="返回表盘">${icon('back')}</button>`;
const title=t=>`${back()}<div class="title">${t}</div>`;
const handle='';
function stopwatch(){return `${title('秒表')}<div class="stop-time">00:00</div><div class="stop-fraction">.00</div><div class="stop-actions"><button data-reset>计圈</button><button class="primary" data-run>${running?'暂停':'开始'}</button></div><button class="stop-records" data-records>记录 0</button>${handle}`}
function controls(){return `<div class="cc"><div class="cc-head">18:36</div><div class="tiles"><button class="tile ${wifi?'on':''}" data-toggle="wifi">${icon('wifi')}Wi-Fi</button><button class="tile ${ble?'on':''}" data-toggle="ble">${icon('ble')}BLE</button></div>${[['sun','亮度',65,199],['sound','音量',40,278]].map(([i,n,v,y])=>`<label class="slider" style="top:${y}px" aria-label="${n}">${icon(i)}<input aria-label="${n}" type="range" value="${v}"><output>${v}</output></label>`).join('')}<div class="cc-bottom">${icon('battery')}82%<button data-go="settings" aria-label="设置">${icon('settings')}</button></div>${handle}</div>`}
function analogFace() {
  const ticks = Array.from({ length: 60 }, (_, index) => {
    const major = index % 5 === 0;
    return `<line x1="233" y1="19" x2="233" y2="${major ? 40 : 28}" transform="rotate(${index * 6} 233 233)" stroke="${major ? '#e6edf8' : '#4d5664'}" stroke-width="${major ? 4 : 2}"/>`;
  }).join('');
  const numerals = Array.from({ length: 12 }, (_, index) => {
    const hour = index + 1;
    const angle = hour * Math.PI / 6;
    return `<text x="${233 + 166 * Math.sin(angle)}" y="${233 - 166 * Math.cos(angle)}">${hour}</text>`;
  }).join('');
  return `<svg class="analog-dial" viewBox="0 0 466 466" role="img" aria-label="指针时钟，10点09分30秒">
    <g>${ticks}</g><g class="dial-numerals">${numerals}</g>
    <g class="dial-date"><text x="233" y="141">周一</text><text x="233" y="172" class="date-number">16</text></g>
    <g class="dial-battery"><circle cx="233" cy="330" r="28"/><circle cx="233" cy="330" r="28" class="battery-progress"/><text x="233" y="331">82</text></g>
    <g class="hour-hand" transform="rotate(304.75 233 233)"><line x1="233" y1="246" x2="233" y2="131"/></g>
    <g class="minute-hand" transform="rotate(57 233 233)"><line x1="233" y1="249" x2="233" y2="85"/></g>
    <circle cx="233" cy="233" r="10" fill="#edf4ff" stroke="none"/>
    <g class="second-hand" transform="rotate(180 233 233)"><line x1="233" y1="263" x2="233" y2="46"/></g>
    <circle cx="233" cy="233" r="5" fill="#9dc6ff" stroke="#000" stroke-width="2"/>
  </svg><button class="analog-launch" data-go="drawer" aria-label="打开应用抽屉"></button>`;
}
function content(p){switch(p){case'analog':return analogFace();case'faces':return `${title('表盘样式')}<div class="rows"><button class="row face-option" data-face="home">${icon('clock')}数字表盘<span>${selectedFace==='home'?'✓':'›'}</span></button><button class="row face-option" data-face="analog">${icon('clock')}指针表盘<span>${selectedFace==='analog'?'✓':'›'}</span></button></div>`;case'boot':return `<div class="brand">${icon('box')}<h2>ToolBox OS</h2></div>`;case'home':return `<div class="status">${icon('wifi')}${icon('battery')}82%</div><div class="time">18:36</div><div class="date">11月16日 · 星期一</div><button class="drawer-open" data-go="drawer" aria-label="打开应用抽屉">${icon('up')}</button>${handle}`;case'drawer':return `${title('应用')}<div class="carousel"><button class="side" data-step="-1" aria-label="上一个应用">${icon('back')}</button><button class="app-icon" data-app aria-label="打开应用">${icon(apps[appIndex][1])}</button><button class="side" data-step="1" aria-label="下一个应用">${icon('right')}</button></div><div class="app-name">${apps[appIndex][0]}</div><div class="dots">${apps.map((_,i)=>i===appIndex?'●':'·').join('')}</div>${handle}`;case'clock':return stopwatch()+`<div class="clock-layer"><div class="clock-tab"><div>18:36</div><div class="clock-battery">${icon('battery')}82%</div></div></div>`;case'control':return controls();case'stopwatch':return stopwatch();case'wifi':return `${title('Wi-Fi')}<div class="rows"><div class="row">Wi-Fi<button class="toggle ${wifi?'on':''}" data-toggle="wifi" aria-label="切换 Wi-Fi"></button></div><div class="network">${wifi?'Studio_2.4G':'Wi-Fi 已关闭'}<small>${wifi?'已连接<br>192.168.1.108':'开启后连接已保存网络'}</small></div><button class="pill wifi-action" data-go="ap">重新配网</button></div>`;case'ap':return `${title('连接手机')}<div class="ap-body"><p>手机连接以下热点</p><div class="network">M5StopWatch-A1B2</div><p>在浏览器打开<br><span class="accent">http://192.168.4.1</span></p><button class="pill" data-go="wifi">退出配网</button></div>`;case'settings':return `${title('设置')}<div class="rows settings-list">${[['clock','表盘样式','faces'],['wifi','Wi-Fi','wifi'],['ble','Bluetooth','bluetooth'],['sun','屏幕','display'],['sound','声音','audio'],['box','系统信息','info']].map(([i,n,t])=>`<button class="row" data-go="${t}">${icon(i)}${n}<span>›</span></button>`).join('')}</div>${handle}`;case'phone':return `<div class="phone"><div>18:36</div><h2>连接 Wi-Fi</h2><p>ToolBox OS</p><form><label for="network">选择网络</label><select id="network"><option>Studio_2.4G</option><option>Home_2.4G</option></select><label for="password">密码</label><input id="password" type="password" required minlength="8" autocomplete="off"><label class="check"><input type="checkbox" data-password>显示密码</label><button class="primary" type="submit">保存并连接</button><p role="status" id="result"></p></form></div>`;case'bluetooth':return `${title('Bluetooth')}<div class="rows"><div class="row">BLE<button class="toggle ${ble?'on':''}" data-toggle="ble" aria-label="切换 BLE"></button></div></div>`;case'display':case'audio':return `${title(p==='display'?'屏幕':'声音')}<label class="slider" style="top:200px">${icon(p==='display'?'sun':'sound')}<input aria-label="调节" type="range" value="65"><output>65</output></label>`;case'info':return `${title('系统信息')}<div class="rows network">ToolBox OS 1.0<small>M5Stack StopWatch<br>ESP32-S3</small></div>`;}}
function renderScreen(el,p){if(p==='home')p=selectedFace;el.dataset.page=p;el.dataset.overlay='0';el.innerHTML=content(p)}
const query=new URLSearchParams(location.search),single=query.get('page');if(single)document.body.classList.add('single');if(query.has('overview'))document.body.classList.add('overview');
document.querySelector('#nav').innerHTML=`<button data-all>全部页面</button>`+Object.entries(pages).map(([id,n])=>`<button data-select="${id}">${n}</button>`).join('');
function gallery(ids){document.querySelector('#gallery').innerHTML=ids.map(id=>`<figure id="${id}"><figcaption>${pages[id]}</figcaption><div class="${id==='phone'?'phone-wrap':'screen'}" data-page="${id}" data-overlay="0">${content(id)}</div></figure>`).join('')}
gallery(single?[single]:Object.keys(pages));
document.addEventListener('click',e=>{const b=e.target.closest('button');if(!b)return;const s=b.closest('.screen');if(b.hasAttribute('data-all'))gallery(Object.keys(pages));if(b.dataset.select)gallery([b.dataset.select]);if(b.dataset.face){selectedFace=b.dataset.face;renderScreen(s,selectedFace)}if(b.dataset.go)renderScreen(s,b.dataset.go);if(b.dataset.step){appIndex=(appIndex+Number(b.dataset.step)+apps.length)%apps.length;renderScreen(s,'drawer')}if(b.hasAttribute('data-app'))renderScreen(s,apps[appIndex][2]);if(b.dataset.toggle){if(b.dataset.toggle==='wifi')wifi=!wifi;else ble=!ble;document.querySelectorAll('[data-toggle]').forEach(t=>t.classList.toggle('on',t.dataset.toggle==='wifi'?wifi:ble));}if(b.hasAttribute('data-run')){if(running)elapsed+=performance.now()-startTime;else startTime=performance.now();running=!running;document.querySelectorAll('[data-run]').forEach(t=>t.textContent=running?'暂停':'开始')}if(b.hasAttribute('data-reset')){elapsed=0;startTime=performance.now()}});
document.addEventListener('input',e=>{if(e.target.type==='range')e.target.nextElementSibling.value=e.target.value;if(e.target.hasAttribute('data-password'))document.querySelector('#password').type=e.target.checked?'text':'password'});
document.addEventListener('submit',e=>{e.preventDefault();document.querySelector('#result').textContent='连接成功 · Studio_2.4G';});
setInterval(()=>{const t=elapsed+(running?performance.now()-startTime:0);document.querySelectorAll('.stop-time').forEach(e=>e.textContent=`${String(Math.floor(t/60000)).padStart(2,'0')}:${String(Math.floor(t/1000)%60).padStart(2,'0')}`);document.querySelectorAll('.stop-fraction').forEach(e=>e.textContent='.'+String(Math.floor(t/10)%100).padStart(2,'0'))},40);
let pointer;
document.addEventListener('pointerdown',e=>{const s=e.target.closest('.screen');if(!s||e.target.closest('input,.settings-list'))return;pointer={x:e.clientX,y:e.clientY,top:e.clientY-s.getBoundingClientRect().top,s};});
const overlayTimers = new WeakMap();
function dismissOverlay(screen) {
  clearTimeout(overlayTimers.get(screen));
  const layers = screen.querySelectorAll('.cc,.clock-layer');
  layers.forEach(layer => layer.classList.add('overlay-leaving'));
  setTimeout(() => {
    if (['clock', 'control'].includes(screen.dataset.page)) renderScreen(screen, 'stopwatch');
    else { layers.forEach(layer => layer.remove()); screen.dataset.overlay = '0'; }
  }, 200);
}
document.addEventListener('pointerup', event => {
  if (!pointer) return;
  const {x, y, top, s} = pointer;
  pointer = null;
  const dx = event.clientX - x, dy = event.clientY - y;
  let level = Number(s.dataset.overlay);
  if (s.dataset.page === 'clock') level = 1;
  if (s.dataset.page === 'control') level = 2;
  if (dy > 45 && (top < 100 || level === 1)) {
    clearTimeout(overlayTimers.get(s));
    if (level === 0) {
      s.insertAdjacentHTML('beforeend', `<div class="clock-layer"><div class="clock-tab"><div>18:36</div><div class="clock-battery">${icon('battery')}82%</div></div></div>`);
      s.dataset.overlay = '1';
      overlayTimers.set(s, setTimeout(() => dismissOverlay(s), 1500));
    } else if (level === 1) {
      s.insertAdjacentHTML('beforeend', controls());
      s.dataset.overlay = '2';
    }
  } else if (dy < -45 && level) {
    dismissOverlay(s);
  } else if (Math.abs(dx) > 45 && s.dataset.page === 'drawer' && !level) {
    appIndex = (appIndex + (dx < 0 ? 1 : -1) + apps.length) % apps.length;
    renderScreen(s, 'drawer');
  }
});

