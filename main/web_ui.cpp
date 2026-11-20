#include <stddef.h>

/* Встроенный лёгкий интерфейс: без внешних библиотек и без SPIFFS. */
static const char INDEX_HTML[] = R"HTML(<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pass-Through UART</title>
<style>
:root{--bg:#0d1117;--card:#161b22;--line:#30363d;--txt:#e6edf3;--muted:#8b949e;--acc:#2f81f7;--ok:#3fb950;--warn:#d29922;--err:#f85149}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);font:14px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif}
header{padding:16px 20px;border-bottom:1px solid var(--line);display:flex;align-items:center;gap:14px;flex-wrap:wrap;background:#11161d}
h1{font-size:17px;margin:0;letter-spacing:.4px}
.badge{font-size:12px;padding:3px 9px;border:1px solid var(--line);border-radius:20px;color:var(--muted)}
.badge.ok{color:var(--ok);border-color:#1f6f38}.badge.warn{color:var(--warn);border-color:#7a5b12}
.dot{display:inline-block;width:7px;height:7px;border-radius:50%;background:var(--err);margin-right:6px}
.dot.on{background:var(--ok)}
.wrap{max-width:1240px;margin:0 auto;padding:18px;display:grid;grid-template-columns:repeat(auto-fit,minmax(330px,1fr));gap:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px}
.card h2{font-size:13px;text-transform:uppercase;letter-spacing:.8px;color:var(--muted);margin:0 0 14px}
.row{display:flex;align-items:center;gap:10px;margin:9px 0;flex-wrap:wrap}
.row label:not(.switch){flex:1;min-width:110px;color:var(--muted)}
.row label.inline-check{flex:0 0 auto;min-width:0;display:inline-flex;align-items:center;
  gap:6px;white-space:nowrap;color:var(--txt)}
select,input,textarea{background:#0d1117;border:1px solid var(--line);color:var(--txt);border-radius:6px;padding:7px 9px;font:inherit;outline:none}
select:focus,input:focus,textarea:focus{border-color:var(--acc)}
input[type=text],input[type=password]{width:150px}
textarea{width:100%;min-height:64px;resize:vertical;font-family:ui-monospace,Consolas,monospace}
button{background:var(--acc);border:0;color:#fff;border-radius:6px;padding:8px 14px;cursor:pointer;font:inherit;font-weight:600}
button:hover{filter:brightness(1.12)}button.sec{background:#21262d;border:1px solid var(--line);font-weight:400}
button:disabled{opacity:.5;cursor:not-allowed}
.langpick{position:relative;flex:0 0 auto}
.langbtn{display:flex;align-items:center;gap:7px;background:#0d1117;border:1px solid var(--line);
  color:var(--txt);border-radius:6px;padding:5px 9px;font-size:13px;font-weight:400}
.langflag,.langmenu img{width:24px;height:16px;object-fit:cover;display:block;border:1px solid #0008;border-radius:2px}
.langmenu{display:none;position:absolute;right:0;top:calc(100% + 4px);z-index:50;min-width:100px;
  background:var(--card);border:1px solid var(--line);border-radius:6px;box-shadow:0 6px 18px #0009;padding:4px}
.langmenu.open{display:block}
.langmenu div{display:flex;align-items:center;gap:9px;padding:5px 7px;border-radius:4px;cursor:pointer}
.langmenu div:hover,.langmenu div.active{background:#2a313b}
.switch{position:relative;width:38px;height:21px;flex:0 0 auto}
.switch input{opacity:0;width:0;height:0}
.slider{position:absolute;inset:0;background:#30363d;border-radius:20px;cursor:pointer;transition:.15s}
.slider:before{content:"";position:absolute;width:15px;height:15px;left:3px;top:3px;background:#c9d1d9;border-radius:50%;transition:.15s}
.switch input:checked+.slider{background:var(--acc)}
.switch input:checked+.slider:before{transform:translateX(17px);background:#fff}
.stats{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:12px}
.stat{background:#0d1117;border:1px solid var(--line);border-radius:6px;padding:8px 10px}
.stat b{display:block;font-size:16px;font-family:ui-monospace,monospace}
.stat span{font-size:11px;color:var(--muted)}
#log{background:#070b10;border:1px solid var(--line);border-radius:6px;height:330px;overflow:auto;padding:10px;
     font:12px/1.5 ui-monospace,Consolas,monospace;white-space:pre-wrap;word-break:break-all}
#log div{border-bottom:1px solid #161b22;padding:1px 0}
.hint{color:var(--muted);font-size:12px;margin-top:10px}
.ssidlist{max-height:132px;overflow:auto;border:1px solid var(--line);border-radius:6px;margin-top:8px}
.ssidlist div{padding:5px 9px;border-bottom:1px solid var(--line);cursor:pointer;font-size:12px}
.ssidlist div:last-child{border-bottom:0}
.ssidlist div:hover{background:#21262d}
.ssidlist span{float:right;color:var(--muted)}
.toast{position:fixed;right:18px;bottom:18px;background:#21262d;border:1px solid var(--line);border-left:3px solid var(--ok);
       padding:10px 14px;border-radius:6px;opacity:0;transform:translateY(8px);transition:.2s;pointer-events:none;z-index:9}
.toast.show{opacity:1;transform:none}
@media(max-width:600px){.wrap{padding:10px}#log{height:260px}}
</style>
</head>
<body>
<header>
  <h1>Pass-Through UART <span class="badge">v<span id="ver">–</span></span></h1>
  <span class="badge">UART1 ⇄ UART2 · Web · Telnet · OTA</span>
  <span class="badge" id="wifiBadge"><span class="dot" id="wifiDot"></span><span id="wifiText">Wi-Fi…</span></span>
  <span class="badge" id="otaBadge">OTA: …</span>
  <span class="badge" id="mqttBadge">MQTT: …</span>
  <span class="badge" id="upBadge">uptime –</span>
  <div class="langpick" data-i18n-title="langTitle">
    <button type="button" class="langbtn" id="langBtn"><img class="langflag" id="langFlag" alt=""><span id="langLabel">RU</span><span style="color:var(--muted)">▾</span></button>
    <div class="langmenu" id="langMenu">
      <div data-lang="ru"><img src="data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIzMCIgaGVpZ2h0PSIyMCIgdmlld0JveD0iMCAwIDMgMiI+PHJlY3Qgd2lkdGg9IjMiIGhlaWdodD0iMiIgZmlsbD0iI2ZmZmZmZiIvPjxyZWN0IHk9IjAuNjY2NyIgd2lkdGg9IjMiIGhlaWdodD0iMC42NjY3IiBmaWxsPSIjMDAzOWE2Ii8+PHJlY3QgeT0iMS4zMzMzIiB3aWR0aD0iMyIgaGVpZ2h0PSIwLjY2NjciIGZpbGw9IiNkNTJiMWUiLz48L3N2Zz4=" alt=""><span>RU</span></div>
      <div data-lang="en"><img src="data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIzMCIgaGVpZ2h0PSIyMCIgdmlld0JveD0iMCAwIDYwIDQwIj48cmVjdCB3aWR0aD0iNjAiIGhlaWdodD0iNDAiIGZpbGw9IiMwMTIxNjkiLz48cGF0aCBkPSJNMCAwTDYwIDQwTTYwIDBMMCA0MCIgc3Ryb2tlPSIjZmZmIiBzdHJva2Utd2lkdGg9IjgiLz48cGF0aCBkPSJNMCAwTDYwIDQwTTYwIDBMMCA0MCIgc3Ryb2tlPSIjQzgxMDJFIiBzdHJva2Utd2lkdGg9IjQiLz48cGF0aCBkPSJNMzAgMFY0ME0wIDIwSDYwIiBzdHJva2U9IiNmZmYiIHN0cm9rZS13aWR0aD0iMTQiLz48cGF0aCBkPSJNMzAgMFY0ME0wIDIwSDYwIiBzdHJva2U9IiNDODEwMkUiIHN0cm9rZS13aWR0aD0iOCIvPjwvc3ZnPg==" alt=""><span>EN</span></div>
    </div>
    <select id="lang" style="display:none"><option value="ru">RU</option><option value="en">EN</option></select>
  </div>
</header>

<div class="wrap">

  <section class="card">
    <h2 data-i18n="hBridge">Сквозной мост UART</h2>
    <div class="row"><label data-i18n="bridgeLbl">UART1 ⇄ UART2 (обе стороны)</label><label class="switch" data-i18n-title="bridgeTitle"><input type="checkbox" id="bridge"><span class="slider"></span></label></div>
    <div class="row"><label data-i18n="echoLbl">Эхо в источник</label><label class="switch" data-i18n-title="echoTitle"><input type="checkbox" id="echo"><span class="slider"></span></label></div>
    <div class="row"><label data-i18n="mirrorLbl">Зеркало на USB</label><label class="switch" data-i18n-title="mirrorTitle"><input type="checkbox" id="mirror"><span class="slider"></span></label></div>
    <div class="row"><label data-i18n="syslogLbl">Системные логи на USB</label><label class="switch" data-i18n-title="syslogTitle"><input type="checkbox" id="syslog"><span class="slider"></span></label></div>
    <div class="row"><label data-i18n="baud1Lbl">UART1 скорость</label><select id="baud1"></select></div>
    <div class="row"><label data-i18n="baud2Lbl">UART2 скорость</label><select id="baud2"></select></div>
    <div class="row"><label data-i18n="autoBaud1Lbl">UART1 автоскорость</label><label class="switch" data-i18n-title="autoBaudTitle"><input type="checkbox" id="autoBaud1"><span class="slider"></span></label></div>
    <div class="row"><label data-i18n="autoBaud2Lbl">UART2 автоскорость</label><label class="switch" data-i18n-title="autoBaudTitle"><input type="checkbox" id="autoBaud2"><span class="slider"></span></label></div>
    <div class="row"><label data-i18n="baudProbeLbl">Команды определения</label><input type="text" id="baudProbe" style="width:200px" value="AT|AT+GMR"></div>
    <div class="hint" id="baudHint"></div>
    <div class="stats">
      <div class="stat"><b id="rx1">0</b><span>UART1 RX</span></div>
      <div class="stat"><b id="tx1">0</b><span>UART1 TX</span></div>
      <div class="stat"><b id="rx2">0</b><span>UART2 RX</span></div>
      <div class="stat"><b id="tx2">0</b><span>UART2 TX</span></div>
    </div>
  </section>

  <section class="card">
    <h2 data-i18n="hSend">Отправка команды</h2>
    <div class="row">
      <label data-i18n="targetLbl">Куда</label>
      <select id="target"><option value="1">UART1</option><option value="2">UART2</option><option value="3" selected data-i18n="bothUart">Оба UART</option><option value="0" data-i18n="usbOnly">Только USB</option></select>
      <label data-i18n="formatLbl">Формат</label><select id="fmt"><option value="text" data-i18n="textFmt">Текст</option><option value="hex">HEX</option></select>
    </div>
    <textarea id="cmd" data-i18n-ph="cmdPh" placeholder='AT&#10;AT+GMR&#10;или 41 54 0D 0A для HEX'></textarea>
    <div class="row" style="margin-top:10px">
      <button id="sendBtn" data-i18n="sendBtn">Отправить ↵</button>
      <button class="sec" data-quick="AT">AT</button>
      <button class="sec" data-quick="AT+GMR">AT+GMR</button>
      <button class="sec" data-quick="AT+CSQ">AT+CSQ</button>
    </div>
    <div class="row"><label data-i18n="usbTargetLbl">Цель по умолчанию (USB/telnet)</label>
      <select id="usbTarget"><option value="1">UART1</option><option value="2">UART2</option><option value="3" data-i18n="bothShort">Оба</option></select>
    </div>
    <div class="row"><label data-i18n="autoMsLbl">Автокоманда ESP32, мс (0 = выкл)</label><input type="text" id="autoMs" style="width:90px"></div>
    <div class="row"><label data-i18n="autoTextLbl">Текст автокоманды</label><input type="text" id="autoText" style="width:170px"></div>
    <div class="hint" data-i18n-html="sendHint">Ctrl+Enter — отправить. Telnet: <code>send 1 AT</code>, <code>sendhex 2 41 54 0D</code>, <code>raw on</code>.</div>
  </section>

  <section class="card">
    <h2 data-i18n="hChannels">Каналы команд (HEX)</h2>
    <div class="row"><label data-i18n="workViaLbl">Работать через</label>
      <select id="cmdMode"><option value="0" data-i18n="urlChan">Адресную строку</option><option value="1">MQTT</option><option value="2" data-i18n="bothChan">Оба канала</option></select>
    </div>
    <div class="row"><label data-i18n="defTargetLbl">Цель по умолчанию</label>
      <select id="cmdTarget"><option value="1">UART1</option><option value="2">UART2</option><option value="3" selected data-i18n="bothUart">Оба UART</option></select>
    </div>
    <div class="row"><label data-i18n="brokerUriLbl">URI брокера</label><input type="text" id="mqttUri" style="width:200px" data-i18n-ph="brokerUriPh" placeholder="host или mqtt://host:1883"></div>
    <div class="hint" id="mqttEff"></div>
    <div class="row"><label data-i18n="baseTopicLbl">Базовый топик</label><input type="text" id="mqttTopic" style="width:200px" placeholder="pass-through-uart"></div>
    <div class="row"><label data-i18n="mqttUserLbl">Пользователь MQTT</label><input type="text" id="mqttUser" style="width:150px"></div>
    <div class="row"><label data-i18n="mqttPassLbl">Пароль MQTT</label><input type="password" id="mqttPass" style="width:150px" data-i18n-ph="mqttPassPh" placeholder="пусто — не менять"></div>
    <div class="row"><button id="cmdApply" data-i18n="cmdApply">Применить каналы</button></div>
    <div class="hint" id="cmdHint"></div>
    <div class="row" style="margin-top:10px"><label data-i18n="urlHexLbl">HEX для адресной строки</label><input type="text" id="urlHex" style="width:150px" value="41 54 0D"></div>
    <div class="row"><button class="sec" id="urlSendBtn" data-i18n="openUrl">Открыть URL</button></div>
    <div class="hint" id="urlExample"></div>
  </section>

  <section class="card" style="grid-column:span 2">
    <h2 data-i18n="hMonitor">Живой монитор</h2>
    <div class="row" style="margin-top:0">
      <span class="badge"><span class="dot" id="wsDot"></span><span id="wsState">WebSocket…</span></span>
      <span class="badge warn" id="clockBadge">Время: синхронизация…</span>
      <select id="logFilter" data-i18n-title="logFilterTitle" style="width:auto">
        <option value="all" data-i18n="logAll">Все сообщения</option>
        <option value="sys" data-i18n="logSys">Системные</option>
        <option value="cmd" data-i18n="logCmd">Команды</option>
      </select>
      <button class="sec" id="clearLog" data-i18n="clearLog">Очистить</button>
      <label class="inline-check"><input type="checkbox" id="autoscroll" checked> <span data-i18n="autoscroll">автопрокрутка</span></label>
      <span class="badge" id="logCount">0 строк</span>
    </div>
    <div id="log"></div>
  </section>

  <section class="card">
    <h2>Wi-Fi (AP + STA)</h2>
    <div class="row"><label data-i18n="apLbl">Точка доступа</label><b id="apState">UART_BRIDGE / 12345678</b></div>
    <div class="row"><label>IP AP</label><b id="apIp">–</b></div>
    <div class="row"><label>IP STA</label><b id="staIp">–</b></div>
    <div class="row"><label>RSSI</label><b id="rssi">–</b></div>
    <div class="row"><label data-i18n="ssidLbl">SSID для подключения</label><input type="text" id="ssid" data-i18n-ph="ssidPh" placeholder="имя сети"></div>
    <div class="row"><label data-i18n="passLbl">Пароль</label><input type="password" id="wpass" data-i18n-ph="passPh" placeholder="пароль"></div>
    <div class="row"><label data-i18n="ssidSearchLbl">Поиск SSID</label><input type="text" id="ssidSearch" data-i18n-ph="ssidSearchPh" placeholder="подстрока..." style="width:150px"><button class="sec" id="scanBtn" data-i18n="scanBtn">Сканировать</button></div>
    <div class="ssidlist" id="ssidList"></div>
    <div class="row"><button id="wifiBtn" data-i18n="wifiBtn">Сохранить и подключиться</button></div>
    <div class="hint" data-i18n="wifiHint">Telnet доступен на порту 23. При подключении к Wi-Fi точка доступа отключается.</div>
  </section>

  <section class="card">
    <h2 data-i18n="hTime">Время (NTP)</h2>
    <div class="row"><label data-i18n="ntpServerLbl">NTP сервер</label><input type="text" id="ntpServer" style="width:200px" placeholder="pool.ntp.org"></div>
    <div class="row"><label data-i18n="ntpTzLbl">Часовой пояс (POSIX TZ)</label><input type="text" id="ntpTz" style="width:130px" placeholder="MSK-3"></div>
    <div class="row"><button id="ntpApply" data-i18n="ntpApply">Применить время</button></div>
    <div class="hint" id="ntpHint" data-i18n-html="ntpHint">После синхронизации журнал и монитор показывают реальное время. Примеры TZ: <code>MSK-3</code>, <code>UTC0</code>, <code>CET-1CEST,M3.5.0,M10.5.0/3</code>.</div>
  </section>

  <section class="card">
    <h2 data-i18n="hOta">Обновление прошивки (OTA)</h2>
    <div class="row"><label data-i18n="fwFileLbl">Файл прошивки</label><input type="file" id="otaFile" accept=".bin,application/octet-stream"></div>
    <div class="row"><button id="otaBtn" data-i18n="flashFileBtn">Прошить из файла</button><button class="sec" id="checkOtaBtn" data-i18n="checkUpdateBtn">Проверить обновления</button><button class="sec" id="rebootBtn" data-i18n="rebootBtn">Перезагрузить</button></div>
    <div class="row"><label class="inline-check" data-i18n-title="autoUpdateTitle"><input type="checkbox" id="autoUpdate"> <span data-i18n="autoUpdateLbl">обновлять автоматически</span></label></div>
    <div class="hint" id="otaVerInfo"></div>
    <div class="row" id="otaUpdateBox" style="display:none"><span class="badge warn" id="otaUpdateInfo"></span><button id="otaRemoteBtn" data-i18n="updateBtn">Обновить</button></div>
    <div class="row"><button id="cfgSaveBtn" data-i18n="cfgSaveBtn">Сохранить настройки</button><button class="sec" id="cfgLoadBtn" data-i18n="cfgLoadBtn">Восстановить</button><input type="file" id="cfgFile" accept="application/json,.json" style="display:none"></div>
    <div class="hint" data-i18n="cfgHint">Файл настроек содержит все параметры, пароли Wi-Fi/MQTT и хеш веб-пароля. Храните его в безопасном месте. После восстановления текущие настройки будут заменены.</div>
    <div class="hint" data-i18n="otaHint">Локальный .bin прошивается кнопкой «Прошить из файла». Версия проверяется в GitHub при загрузке модуля и раз в сутки; если найдена новая — она появится выше. С включённой галкой автообновления прошивка ставится без подтверждения.</div>
  </section>

  <section class="card">
    <h2 data-i18n="hSecurity">Безопасность</h2>
    <div class="row"><label data-i18n="oldPassLbl">Текущий пароль</label><input type="password" id="oldPass" style="width:160px"></div>
    <div class="row"><label data-i18n="newPassLbl">Новый пароль</label><input type="password" id="newPass" style="width:160px"></div>
    <div class="row"><label data-i18n="newPass2Lbl">Повторите новый</label><input type="password" id="newPass2" style="width:160px"></div>
    <div class="row"><button id="passwdBtn" data-i18n="passwdBtn">Сменить пароль</button></div>
    <div class="hint" data-i18n-html="securityHint">Пароль — это путь в адресе: <code>http://&lt;ip&gt;/sec</code>, по умолчанию <code>sec</code>. Он же запрашивается в Telnet. Разрешены латиница, цифры и <code>. _ - ~</code>. Меняется здесь или командой <code>passwd &lt;новый&gt;</code>. После смены откройте новый адрес.</div>
  </section>
</div>

<div class="toast" id="toast"></div>

<script>
const $=id=>document.getElementById(id);

/* --- Язык интерфейса: RU / EN. Выбор хранится в localStorage браузера. --- */
const LANG_KEY='ptuart.lang';
let LANG=localStorage.getItem(LANG_KEY)||'ru';
if(LANG!=='ru'&&LANG!=='en')LANG='ru';
const I18N={
 ru:{
  langTitle:'Язык интерфейса',
  hBridge:'Сквозной мост UART', bridgeLbl:'UART1 ⇄ UART2 (обе стороны)',
  bridgeTitle:'Включить/выключить сквозную пересылку',
  echoLbl:'Эхо в источник', echoTitle:'Дублировать принятые байты в источник',
  mirrorLbl:'Зеркало на USB', mirrorTitle:'Показывать трафик в USB',
  syslogLbl:'Системные логи на USB', syslogTitle:'Выводить логи ESP-IDF в USB',
  baud1Lbl:'UART1 скорость', baud2Lbl:'UART2 скорость',
  autoBaud1Lbl:'UART1 автоскорость', autoBaud2Lbl:'UART2 автоскорость',
  autoBaudTitle:'Определить скорость по ответу на команды', baudProbeLbl:'Команды определения',
  hSend:'Отправка команды', targetLbl:'Куда', bothUart:'Оба UART', usbOnly:'Только USB',
  formatLbl:'Формат', textFmt:'Текст', cmdPh:'AT\nAT+GMR\nили 41 54 0D 0A для HEX',
  sendBtn:'Отправить ↵', usbTargetLbl:'Цель по умолчанию (USB/telnet)', bothShort:'Оба',
  autoMsLbl:'Автокоманда ESP32, мс (0 = выкл)', autoTextLbl:'Текст автокоманды',
  sendHint:'Ctrl+Enter — отправить. Telnet: <code>send 1 AT</code>, <code>sendhex 2 41 54 0D</code>, <code>raw on</code>.',
  hChannels:'Каналы команд (HEX)', workViaLbl:'Работать через', urlChan:'Адресную строку',
  bothChan:'Оба канала', defTargetLbl:'Цель по умолчанию',
  brokerUriLbl:'URI брокера', brokerUriPh:'host или mqtt://host:1883',
  baseTopicLbl:'Базовый топик', mqttUserLbl:'Пользователь MQTT', mqttPassLbl:'Пароль MQTT',
  mqttPassPh:'пусто — не менять', cmdApply:'Применить каналы', urlHexLbl:'HEX для адресной строки', openUrl:'Открыть URL',
  hMonitor:'Живой монитор', clearLog:'Очистить', autoscroll:'автопрокрутка',
  logFilterTitle:'Показывать в журнале', logAll:'Все сообщения', logSys:'Системные', logCmd:'Команды',
  apLbl:'Точка доступа', ssidLbl:'SSID для подключения', ssidPh:'имя сети',
  passLbl:'Пароль', passPh:'пароль', ssidSearchLbl:'Поиск SSID', ssidSearchPh:'подстрока...',
  scanBtn:'Сканировать', wifiBtn:'Сохранить и подключиться',
  wifiHint:'Telnet доступен на порту 23. При подключении к Wi-Fi точка доступа отключается.',
  hTime:'Время (NTP)', ntpServerLbl:'NTP сервер', ntpTzLbl:'Часовой пояс (POSIX TZ)', ntpApply:'Применить время',
  ntpHint:'После синхронизации журнал и монитор показывают реальное время. Примеры TZ: <code>MSK-3</code>, <code>UTC0</code>, <code>CET-1CEST,M3.5.0,M10.5.0/3</code>.',
  hOta:'Обновление прошивки (OTA)', fwFileLbl:'Файл прошивки', flashFileBtn:'Прошить из файла',
  rebootBtn:'Перезагрузить', checkUpdateBtn:'Проверить обновления', updateBtn:'Обновить',
  autoUpdateLbl:'обновлять автоматически',
  autoUpdateTitle:'Проверять версию на GitHub при загрузке и раз в сутки; при наличии новой версии обновляться без подтверждения',
  updateInfo:'Доступна версия {remote} (сейчас {local}).', updateNone:'Обновлений нет',
  updateCheckErr:'не удалось проверить версию', updateCheckStarted:'проверка запущена',
  fwFileMissing:'выберите файл прошивки',
  cfgSaveBtn:'Сохранить настройки', cfgLoadBtn:'Восстановить',
  cfgHint:'Файл настроек содержит все параметры, пароли Wi-Fi/MQTT и хеш веб-пароля. Храните его в безопасном месте. После восстановления текущие настройки будут заменены.',
  otaHint:'Локальный .bin прошивается кнопкой «Прошить из файла». Версия проверяется в GitHub при загрузке модуля и раз в сутки; если найдена новая — она появится выше. С включённой галкой автообновления прошивка ставится без подтверждения.',
  hSecurity:'Безопасность', oldPassLbl:'Текущий пароль', newPassLbl:'Новый пароль',
  newPass2Lbl:'Повторите новый', passwdBtn:'Сменить пароль',
  securityHint:'Пароль — это путь в адресе: <code>http://&lt;ip&gt;/sec</code>, по умолчанию <code>sec</code>. Он же запрашивается в Telnet. Разрешены латиница, цифры и <code>. _ - ~</code>. Меняется здесь или командой <code>passwd &lt;новый&gt;</code>. После смены откройте новый адрес.',
  wifiOnline:'STA онлайн', wifiAp:'AP режим', clockSync:'Время: синхронизация…', clockPrefix:'Время: ',
  mqttOff:'выкл', mqttOnline:'онлайн', mqttOffline:'офлайн',
  apOff:'выключена (есть STA)',
  baudHintBase:'Пины: UART1 TX4/RX5, UART2 TX25/RX26, USB — UART0 (TX1/RX3, 115200). Изменения сохраняются сразу.',
  baudDetected:'определено', baudSearching:'поиск...',
  effAddr:'Эффективный адрес: ', mqttHintNoUri:'Можно host или mqtt://host:1883; без порта — 1883.',
  curTarget:'Текущая цель: ', logCount:'{n} строк в буфере',
  logCountTitle:'В мониторе показаны последние 300 строк + живой поток',
  wsReconnect:'переподключение…',
  cmdHint0:'Команды принимаются только через адресную строку.',
  cmdHint1:'Команды принимаются только через MQTT.',
  cmdHint2:'Оба канала активны одновременно.',
  urlSendLabel:'Отправка: ', urlReadLabel:'Чтение ответа: ', mqttCodeLabel:'MQTT код: ',
  mqttTargetLabel:'MQTT цель: ', mqttOutLabel:'MQTT исходящие: ', trafficLabel:'трафик',
  errLink:'ошибка связи', foundAps:'найдено точек: ', passwordMissing:'Password is not specified',
  passwordMismatch:'Passwords do not match',
  exportErr:'ошибка экспорта настроек', cfgSavedFile:'настройки сохранены в файл',
  saveFileErr:'ошибка сохранения файла', restoreConfirm:'Восстановить настройки из {f}? Текущие настройки будут заменены.',
  readFileErr:'ошибка чтения файла настроек', rebootConfirm:'Перезагрузить ESP32?',
  otaErr:'ошибка: ', otaNever:'не выполнялась', otaLoading:'загрузка...', otaDone:'успешно, перезагрузка',
  'msg:настройки сохранены':'настройки сохранены',
  'msg:настройки восстановлены из файла':'настройки восстановлены из файла',
  'msg:журнал очищен':'журнал очищен',
  'msg:нет тела запроса':'нет тела запроса', 'msg:неверный JSON':'неверный JSON',
  'msg:неверный формат файла настроек':'неверный формат файла настроек',
  'msg:неверный хеш пароля в файле':'неверный хеш пароля в файле',
  'msg:в файле нет настроек':'в файле нет настроек',
  'msg:нет данных':'нет данных', 'msg:нужен URL':'нужен URL',
  'msg:не удалось сформировать файл':'не удалось сформировать файл',
  'msg:OTA запущена':'OTA запущена', 'msg:перезагрузка':'перезагрузка',
  'msg:проверка запущена':'проверка запущена',
  'msg:файл прошивки пуст':'файл прошивки пуст',
  'msg:ошибка приёма файла':'ошибка приёма файла'
 },
 en:{
  langTitle:'Interface language',
  hBridge:'UART transparent bridge', bridgeLbl:'UART1 ⇄ UART2 (bidirectional)',
  bridgeTitle:'Enable/disable transparent forwarding',
  echoLbl:'Echo to source', echoTitle:'Send received bytes back to the source',
  mirrorLbl:'USB mirror', mirrorTitle:'Show traffic on USB',
  syslogLbl:'System logs to USB', syslogTitle:'Output ESP-IDF logs to USB',
  baud1Lbl:'UART1 baud rate', baud2Lbl:'UART2 baud rate',
  autoBaud1Lbl:'UART1 auto baud', autoBaud2Lbl:'UART2 auto baud',
  autoBaudTitle:'Detect baud rate from command replies', baudProbeLbl:'Detection commands',
  hSend:'Send command', targetLbl:'Target', bothUart:'Both UART', usbOnly:'USB only',
  formatLbl:'Format', textFmt:'Text', cmdPh:'AT\nAT+GMR\nor 41 54 0D 0A for HEX',
  sendBtn:'Send ↵', usbTargetLbl:'Default target (USB/telnet)', bothShort:'Both',
  autoMsLbl:'ESP32 auto command, ms (0 = off)', autoTextLbl:'Auto command text',
  sendHint:'Ctrl+Enter — send. Telnet: <code>send 1 AT</code>, <code>sendhex 2 41 54 0D</code>, <code>raw on</code>.',
  hChannels:'Command channels (HEX)', workViaLbl:'Use channel', urlChan:'URL query',
  bothChan:'Both channels', defTargetLbl:'Default target',
  brokerUriLbl:'Broker URI', brokerUriPh:'host or mqtt://host:1883',
  baseTopicLbl:'Base topic', mqttUserLbl:'MQTT user', mqttPassLbl:'MQTT password',
  mqttPassPh:'empty — keep current', cmdApply:'Apply channels', urlHexLbl:'HEX for URL', openUrl:'Open URL',
  hMonitor:'Live monitor', clearLog:'Clear', autoscroll:'autoscroll',
  logFilterTitle:'Show in log', logAll:'All messages', logSys:'System', logCmd:'Commands',
  apLbl:'Access point', ssidLbl:'SSID to connect', ssidPh:'network name',
  passLbl:'Password', passPh:'password', ssidSearchLbl:'Find SSID', ssidSearchPh:'substring...',
  scanBtn:'Scan', wifiBtn:'Save and connect',
  wifiHint:'Telnet is available on port 23. When STA connects, the access point is switched off.',
  hTime:'Time (NTP)', ntpServerLbl:'NTP server', ntpTzLbl:'Time zone (POSIX TZ)', ntpApply:'Apply time',
  ntpHint:'After synchronization the log and monitor show real time. TZ examples: <code>MSK-3</code>, <code>UTC0</code>, <code>CET-1CEST,M3.5.0,M10.5.0/3</code>.',
  hOta:'Firmware update (OTA)', fwFileLbl:'Firmware file', flashFileBtn:'Flash from file',
  rebootBtn:'Reboot', checkUpdateBtn:'Check for updates', updateBtn:'Update',
  autoUpdateLbl:'update automatically',
  autoUpdateTitle:'Check the version on GitHub at boot and once a day; install a newer firmware without confirmation',
  updateInfo:'Version {remote} is available (current {local}).', updateNone:'No updates',
  updateCheckErr:'failed to check version', updateCheckStarted:'check started',
  fwFileMissing:'select a firmware file',
  cfgSaveBtn:'Save settings', cfgLoadBtn:'Restore',
  cfgHint:'The settings file contains all parameters, Wi-Fi/MQTT passwords and the web password hash. Store it securely. Restoring replaces the current settings.',
  otaHint:'A local .bin is flashed with "Flash from file". The version is checked on GitHub at boot and once a day; a newer one will be shown above. With automatic updates enabled, the new firmware is installed without confirmation.',
  hSecurity:'Security', oldPassLbl:'Current password', newPassLbl:'New password',
  newPass2Lbl:'Repeat new password', passwdBtn:'Change password',
  securityHint:'The password is the URL path: <code>http://&lt;ip&gt;/sec</code>, default <code>sec</code>. The same password is used for Telnet. Allowed: letters, digits and <code>. _ - ~</code>. Change it here or with <code>passwd &lt;new&gt;</code>. After changing, open the new address.',
  wifiOnline:'STA online', wifiAp:'AP mode', clockSync:'Time: syncing…', clockPrefix:'Time: ',
  mqttOff:'off', mqttOnline:'online', mqttOffline:'offline',
  apOff:'disabled (STA connected)',
  baudHintBase:'Pins: UART1 TX4/RX5, UART2 TX25/RX26, USB — UART0 (TX1/RX3, 115200). Changes are saved immediately.',
  baudDetected:'detected', baudSearching:'searching...',
  effAddr:'Effective URI: ', mqttHintNoUri:'Use host or mqtt://host:1883; default port is 1883.',
  curTarget:'Current target: ', logCount:'{n} lines in buffer',
  logCountTitle:'The monitor shows the last 300 lines plus the live stream',
  wsReconnect:'reconnecting…',
  cmdHint0:'Commands are accepted only via the URL query.',
  cmdHint1:'Commands are accepted only via MQTT.',
  cmdHint2:'Both channels are active at the same time.',
  urlSendLabel:'Send: ', urlReadLabel:'Read reply: ', mqttCodeLabel:'MQTT code: ',
  mqttTargetLabel:'MQTT target: ', mqttOutLabel:'MQTT outgoing: ', trafficLabel:'traffic',
  errLink:'connection error', foundAps:'APs found: ', passwordMissing:'Password is not specified',
  passwordMismatch:'Passwords do not match',
  exportErr:'settings export failed', cfgSavedFile:'settings saved to file',
  saveFileErr:'failed to save file', restoreConfirm:'Restore settings from {f}? Current settings will be replaced.',
  readFileErr:'failed to read settings file', rebootConfirm:'Reboot ESP32?',
  otaErr:'error: ', otaNever:'never run', otaLoading:'downloading...', otaDone:'success, rebooting',
  'msg:настройки сохранены':'settings saved',
  'msg:настройки восстановлены из файла':'settings restored from file',
  'msg:журнал очищен':'log cleared',
  'msg:нет тела запроса':'no request body', 'msg:неверный JSON':'invalid JSON',
  'msg:неверный формат файла настроек':'invalid settings file format',
  'msg:неверный хеш пароля в файле':'invalid password hash in file',
  'msg:в файле нет настроек':'no settings in file',
  'msg:нет данных':'no data', 'msg:нужен URL':'URL required',
  'msg:не удалось сформировать файл':'failed to build file',
  'msg:OTA запущена':'OTA started', 'msg:перезагрузка':'rebooting',
  'msg:проверка запущена':'check started',
  'msg:файл прошивки пуст':'firmware file is empty',
  'msg:ошибка приёма файла':'file receive error',
  'msg:Password is not specified':'Password is not specified',
  'msg:Wrong password':'Wrong password',
  'msg:Invalid password: A-Z a-z 0-9 . _ - ~ (max 32)':'Invalid password: A-Z a-z 0-9 . _ - ~ (max 32)',
  'msg:Unauthorized':'Unauthorized', 'msg:ok':'ok'
 }
};
const FLAG_SRC={
 ru:'data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIzMCIgaGVpZ2h0PSIyMCIgdmlld0JveD0iMCAwIDMgMiI+PHJlY3Qgd2lkdGg9IjMiIGhlaWdodD0iMiIgZmlsbD0iI2ZmZmZmZiIvPjxyZWN0IHk9IjAuNjY2NyIgd2lkdGg9IjMiIGhlaWdodD0iMC42NjY3IiBmaWxsPSIjMDAzOWE2Ii8+PHJlY3QgeT0iMS4zMzMzIiB3aWR0aD0iMyIgaGVpZ2h0PSIwLjY2NjciIGZpbGw9IiNkNTJiMWUiLz48L3N2Zz4=',
 en:'data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIzMCIgaGVpZ2h0PSIyMCIgdmlld0JveD0iMCAwIDYwIDQwIj48cmVjdCB3aWR0aD0iNjAiIGhlaWdodD0iNDAiIGZpbGw9IiMwMTIxNjkiLz48cGF0aCBkPSJNMCAwTDYwIDQwTTYwIDBMMCA0MCIgc3Ryb2tlPSIjZmZmIiBzdHJva2Utd2lkdGg9IjgiLz48cGF0aCBkPSJNMCAwTDYwIDQwTTYwIDBMMCA0MCIgc3Ryb2tlPSIjQzgxMDJFIiBzdHJva2Utd2lkdGg9IjQiLz48cGF0aCBkPSJNMzAgMFY0ME0wIDIwSDYwIiBzdHJva2U9IiNmZmYiIHN0cm9rZS13aWR0aD0iMTQiLz48cGF0aCBkPSJNMzAgMFY0ME0wIDIwSDYwIiBzdHJva2U9IiNDODEwMkUiIHN0cm9rZS13aWR0aD0iOCIvPjwvc3ZnPg=='
};
/* Журнал пишется прошивкой по-русски. Переводим его на клиенте, чтобы смена
   языка меняла и уже загруженные строки. Параметры (IP, числа, топики) остаются. */
const LOG_DICT=[
 ['Pass-Through UART готов к работе','Pass-Through UART is ready'],
 ['запуск ','starting '],
 ['NVS нет сохранённой конфигурации, используем значения по умолчанию','NVS: no saved configuration, using defaults'],
 ['конфигурация загружена из NVS','configuration loaded from NVS'],
 ['не удалось установить драйвер UART0 (USB): ','failed to set up UART0 (USB) driver: '],
 ['не удалось запустить HTTP сервер','failed to start HTTP server'],
 ['не удалось создать клиент','failed to create client'],
 ['не удалось открыть NVS: ','failed to open NVS: '],
 ['не удалось ','failed to '],
 ['STA подключён, IP ','STA connected, IP '],
 ['STA отключён, повторная попытка...','STA disconnected, retrying...'],
 ['AP включён повторно (STA отключён)','AP re-enabled (STA disconnected)'],
 ['AP включён повторно','AP re-enabled'],
 ['AP выключен (есть STA)','AP disabled (STA connected)'],
 ['AP выключен, остался только STA','AP disabled, only STA remains'],
 ['клиент подключён к точке доступа','client connected to access point'],
 ['клиент отключён от точки доступа','client disconnected from access point'],
 ['настройки восстановлены из файла','settings restored from file'],
 ['настройки сохранены в файл','settings saved to file'],
 ['настройки сохранены','settings saved'],
 ['NTP синхронизировано: ','NTP synchronized: '],
 ['NTP сервер: ','NTP server: '],
 ['подключение к ','connecting to '],
 ['подписка: ','subscribed to '],
 ['цель MQTT: ','MQTT target: '],
 ['команда HEX','HEX command'],
 ['ошибка сохранения: ','save error: '],
 ['ошибка MQTT','MQTT error'],
 ['OTA: начало обновления','OTA: update started'],
 ['OTA: прошивка установлена, перезагрузка','OTA: firmware installed, rebooting'],
 ['OTA: прошивка подтверждена','OTA: firmware confirmed'],
 ['OTA уже выполняется','OTA already in progress'],
  ['доступна прошивка v','firmware v'],
  [' (сейчас v',' (current v'],
  ['проверка версии: актуальна v','version check: up to date v'],
  ['не удалось проверить версию прошивки','failed to check firmware version'],
  ['автообновление прошивки','automatic firmware update'],
 ['OTA запущена','OTA started'],
 ['журнал очищен','log cleared'],
 ['журнал пуст','log is empty'],
 ['нет ответа на ','no reply to '],
 [', оставлена скорость ',', kept baud rate '],
 [' бод определена по команде',' baud detected from command'],
 ['скорость ','baud rate '],
 [' бод',' baud'],
 ['ошибка: ','error: '],
 ['ошибка','error'],
 ['подключён','connected'],
 ['отключён','disconnected'],
 ['отключен','disconnected'],
 ['байт','bytes'],
 ['включён','enabled'],
 ['готов к работе','is ready']
].sort((a,b)=>b[0].length-a[0].length);
function trLog(s){
  if(LANG!=='en')return s;
  for(const p of LOG_DICT)s=s.split(p[0]).join(p[1]);
  return s;
}
function t(k,vars){
  let s=(I18N[LANG]&&I18N[LANG][k])||(I18N.ru[k]||k);
  if(vars)for(const p in vars)s=s.split('{'+p+'}').join(vars[p]);
  return s;
}
function trMsg(m){return t('msg:'+m)!==('msg:'+m)?t('msg:'+m):m;}
/* OTA-статус приходит с устройства — переводим известные значения и префикс ошибки */
function trOta(s){
  s=s||'–';
  if(LANG==='en'){
    if(s==='не выполнялась')s=t('otaNever');
    else if(s==='загрузка...')s=t('otaLoading');
    else if(s==='успешно, перезагрузка')s=t('otaDone');
    if(s.indexOf('ошибка: ')===0)s=t('otaErr')+s.slice(8);
  }
  return s;
}
function renderOtaUpdate(s){
  const local=s.version||'–', remote=s.ota_remote||'–';
  $('otaVerInfo').textContent='v'+local+' · GitHub: v'+remote;
  const avail=!!s.ota_available;
  $('otaUpdateBox').style.display=avail?'':'none';
  if(avail)$('otaUpdateInfo').textContent=t('updateInfo',{remote:remote,local:local});
}
/* Время работы считаем на клиенте, чтобы не показывать русский текст в EN */
function formatUptime(sec){
  sec=Math.max(0,Math.floor(sec||0));
  const d=Math.floor(sec/86400), h=Math.floor(sec%86400/3600),
        m=Math.floor(sec%3600/60), s=sec%60, p=n=>String(n).padStart(2,'0');
  if(d)return d+(LANG==='en'?' d ':' д ')+p(h)+(LANG==='en'?' h ':' ч ')+p(m)+(LANG==='en'?' min':' мин');
  if(h)return h+(LANG==='en'?' h ':' ч ')+p(m)+(LANG==='en'?' min':' мин');
  if(m)return m+(LANG==='en'?' min ':' мин ')+p(s)+(LANG==='en'?' s':' с');
  return s+(LANG==='en'?' s':' с');
}
function applyLang(){
  document.documentElement.lang=LANG;
  document.querySelectorAll('[data-i18n]').forEach(el=>{el.textContent=t(el.dataset.i18n);});
  document.querySelectorAll('[data-i18n-html]').forEach(el=>{el.innerHTML=t(el.dataset.i18nHtml);});
  document.querySelectorAll('[data-i18n-ph]').forEach(el=>{el.placeholder=t(el.dataset.i18nPh);});
  document.querySelectorAll('[data-i18n-title]').forEach(el=>{el.title=t(el.dataset.i18nTitle);});
  $('lang').value=LANG;
  $('langFlag').src=FLAG_SRC[LANG];
  $('langLabel').textContent=LANG.toUpperCase();
  document.querySelectorAll('#langMenu [data-lang]').forEach(el=>el.classList.toggle('active',el.dataset.lang===LANG));
  refresh(true);updateCmdModeUI();tickClock();rerenderLog();
}
$('lang').onchange=e=>{LANG=e.target.value;localStorage.setItem(LANG_KEY,LANG);applyLang();};
/* Выпадающий список с картинками флагов: в родном <select> картинки не рисуются */
$('langBtn').onclick=e=>{e.stopPropagation();$('langMenu').classList.toggle('open');};
document.querySelectorAll('#langMenu [data-lang]').forEach(el=>el.onclick=()=>{
  const v=el.dataset.lang;
  $('lang').value=v;
  $('lang').dispatchEvent(new Event('change'));
  $('langMenu').classList.remove('open');
});
document.addEventListener('click',()=>$('langMenu').classList.remove('open'));

const BAUDS=[1200,2400,4800,9600,19200,38400,57600,115200,230400,460800,921600];
function fillBaud(sel){sel.innerHTML=BAUDS.map(b=>`<option value="${b}">${b}</option>`).join('');}
fillBaud($('baud1'));fillBaud($('baud2'));

let toastTimer;
function toast(msg,err){const t=$('toast');t.textContent=msg;t.style.borderLeftColor=err?'var(--err)':'var(--ok)';t.classList.add('show');clearTimeout(toastTimer);toastTimer=setTimeout(()=>t.classList.remove('show'),2600);}
async function api(path,body){
  const ctl=new AbortController(), timer=setTimeout(()=>ctl.abort(),5000);
  const opts=body?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{};
  opts.signal=ctl.signal;
  try{
    const r=await fetch(path,opts);
    if(r.status===401){location.href=location.pathname;return null;}  /* cookie мог умереть при ребуте/смене пароля */
    const j=await r.json(); if(body)toast(trMsg(j.msg||'ok'),!j.ok); return j;
  }catch(e){ if(body)toast(t('errLink'),true); return null; }
  finally{ clearTimeout(timer); }
}
/* Автообновление не должно перезаписывать значения, которые пользователь ещё не сохранил */
const dirty=new Set();
const CFG_IDS=['baud1','baud2','bridge','echo','mirror','syslog',
  'autoBaud1','autoBaud2','baudProbe',
  'usbTarget','autoMs','autoText','ssid','autoUpdate',
  'ntpServer','ntpTz','cmdMode','cmdTarget','mqttUri','mqttTopic','mqttUser'];
CFG_IDS.forEach(id=>{const el=$(id);if(!el)return;
  el.addEventListener('input',()=>dirty.add(id));
  el.addEventListener('change',()=>dirty.add(id));
});
function clearDirty(ids){ids.forEach(id=>dirty.delete(id));}
async function saveCfg(ids,body){const j=await api('/api/config',body);if(j&&j.ok)clearDirty(ids);return j;}
$('autoUpdate').onchange=e=>saveCfg(['autoUpdate'],{auto_update:e.target.checked});
function setVal(id,v){const el=$(id);if(el&&!dirty.has(id)&&document.activeElement!==el)el.value=v;}
const targetName=v=>v===0?'USB':v===1?'UART1':v===2?'UART2':t('bothUart');
const hexClean=s=>(s||'').replace(/\s+/g,'');   /* пробелы в HEX разрешены, но наружу уходит без них */

let clockOffset=null, tzOffset=0, clockSynced=false;
function tickClock(){
  const b=$('clockBadge');
  if(!clockSynced||clockOffset===null){b.textContent=t('clockSync');b.className='badge warn';return;}
  const d=new Date(Date.now()+clockOffset+tzOffset*60000), p=n=>String(n).padStart(2,'0');
  b.textContent=t('clockPrefix')+p(d.getUTCDate())+'.'+p(d.getUTCMonth()+1)+' '+p(d.getUTCHours())+':'+p(d.getUTCMinutes())+':'+p(d.getUTCSeconds());
  b.className='badge ok';
}
setInterval(tickClock,1000);

let refreshing=false;
async function refresh(force){
  if(refreshing&&!force)return; refreshing=true;
  const s=await api('/api/status');
  refreshing=false;
  if(!s)return;
  $('ver').textContent=s.version;
  $('upBadge').textContent='uptime '+formatUptime(s.uptime);
  $('wifiText').textContent=s.wifi.connected?t('wifiOnline'):t('wifiAp');
  $('wifiDot').className='dot'+(s.wifi.connected?' on':'');
  $('otaBadge').textContent='OTA: '+trOta(s.ota_last);
  $('bridge').checked=s.bridge;$('echo').checked=s.echo;$('mirror').checked=s.mirror;$('syslog').checked=s.syslog;
  $('autoBaud1').checked=!!s.auto_baud1;$('autoBaud2').checked=!!s.auto_baud2;
  if(s.time){
    clockSynced=!!s.time.synced; tzOffset=s.time.offset||0;
    if(s.time.epoch>0) clockOffset=s.time.epoch*1000-Date.now();
    $('clockBadge').title='NTP: '+s.time.server+' · TZ '+s.time.tz+(s.time.str?' · '+s.time.str:'');
  }
  tickClock();
  setVal('baud1',String(s.baud1));setVal('baud2',String(s.baud2));
  setVal('baudProbe',s.baud_probe||'AT|AT+GMR');
  $('baudHint').textContent=t('baudHintBase')+
    (s.auto_baud1?(' · UART1: '+(s.baud1_detected?(t('baudDetected')+' '+s.baud1_detected):t('baudSearching'))):'')+
    (s.auto_baud2?(' · UART2: '+(s.baud2_detected?(t('baudDetected')+' '+s.baud2_detected):t('baudSearching'))):'');
  setVal('usbTarget',String(s.usb_target));setVal('autoMs',String(s.auto_ms));setVal('autoText',s.auto_text||'');
  setVal('ssid',s.wifi.ssid||'');
  if(!dirty.has('autoUpdate'))$('autoUpdate').checked=!!s.auto_update;
  renderOtaUpdate(s);
  setVal('ntpServer',s.time?s.time.server:'pool.ntp.org');setVal('ntpTz',s.time?s.time.tz:'MSK-3');
  const mq=s.mqtt||{}, mode=mq.mode||0;
  setVal('cmdMode',String(mode));setVal('cmdTarget',String(mq.target||3));
  setVal('mqttUri',mq.uri||'');setVal('mqttTopic',mq.topic||'');setVal('mqttUser',mq.user||'');
  const mqTarget=(mq.target_effective!==undefined)?mq.target_effective:mq.target;
  $('mqttEff').textContent=(mq.uri?(t('effAddr')+(mq.uri_effective||mq.uri)):
    t('mqttHintNoUri'))+
    (mq.mode? (' · '+t('curTarget')+targetName(mqTarget)) : '');
  const mqb=$('mqttBadge');
  mqb.textContent='MQTT: '+(mode===0?t('mqttOff'):(mq.connected?t('mqttOnline'):t('mqttOffline')));
  mqb.className='badge '+(mode!==0&&mq.connected?'ok':'warn');
  updateCmdModeUI();
  $('apState').textContent=s.wifi.ap_enabled?'UART_BRIDGE / 12345678':t('apOff');
  $('apIp').textContent=s.wifi.ap_enabled?s.wifi.ap_ip:'–';$('staIp').textContent=s.wifi.sta_ip||'–';
  $('rssi').textContent=s.wifi.connected?s.wifi.rssi+' dBm':'–';
  $('rx1').textContent=s.counters.rx1;$('tx1').textContent=s.counters.tx1;
  $('rx2').textContent=s.counters.rx2;$('tx2').textContent=s.counters.tx2;
  $('logCount').textContent=t('logCount',{n:s.log_lines});
  $('logCount').title=t('logCountTitle');
}

const LOG_RAW=[];   /* оригинальные строки: при смене языка переводим их заново */
/*SYS/OTA/WIFI — системные, остальные теги — трафик и команды UART/USB */
const LOG_FILTER={
  sys:s=>/\[(SYS|OTA|WIFI)\]/.test(s),
  cmd:s=>/\[(UART1|UART2|USB|LOCAL)/.test(s)
};
function logVisible(s){
  const f=$('logFilter')?$('logFilter').value:'all';
  return f==='all'||(LOG_FILTER[f]?LOG_FILTER[f](s):true);
}
function logLine(line){
  LOG_RAW.push(line);if(LOG_RAW.length>600)LOG_RAW.shift();
  if(!logVisible(line))return;
  const box=$('log'),d=document.createElement('div');d.textContent=trLog(line);box.appendChild(d);
  while(box.childNodes.length>600)box.removeChild(box.firstChild);
  if($('autoscroll').checked)box.scrollTop=box.scrollHeight;
}
function rerenderLog(){
  const box=$('log');box.innerHTML='';
  LOG_RAW.filter(logVisible).forEach(s=>{const d=document.createElement('div');d.textContent=trLog(s);box.appendChild(d);});
  if($('autoscroll').checked)box.scrollTop=box.scrollHeight;
}
$('logFilter').onchange=rerenderLog;
async function loadLog(){
  const j=await api('/api/log?n=300'); if(!j)return;
  LOG_RAW.length=0;$('log').innerHTML='';(j.lines||[]).forEach(logLine);
}
function wsConnect(){
  const proto=location.protocol==='https:'?'wss:':'ws:';
  const ws=new WebSocket(proto+'//'+location.host+'/ws');
  $('wsDot').className='dot';$('wsState').textContent='WebSocket…';
  ws.onopen=()=>{$('wsDot').className='dot on';$('wsState').textContent='live';};
  ws.onmessage=e=>{try{const j=JSON.parse(e.data);logLine(j.line||'');}catch(_){}};
  ws.onclose=async()=>{
    $('wsState').textContent=t('wsReconnect');
    try{const r=await fetch('/api/status');if(r.status===401){location.href=location.pathname;return;}}catch(_){}
    setTimeout(wsConnect,1500);
  };
  ws.onerror=()=>ws.close();
}

function configPayload(){
  return {baud1:+$('baud1').value,baud2:+$('baud2').value,
    bridge:$('bridge').checked,echo:$('echo').checked,mirror:$('mirror').checked,syslog:$('syslog').checked,
    auto_baud1:$('autoBaud1').checked,auto_baud2:$('autoBaud2').checked,baud_probe:$('baudProbe').value,
    usb_target:+$('usbTarget').value,auto_ms:+$('autoMs').value||0,auto_text:$('autoText').value,
    cmd_mode:+$('cmdMode').value,cmd_target:+$('cmdTarget').value,
    mqtt_uri:$('mqttUri').value,mqtt_topic:$('mqttTopic').value,
    mqtt_user:$('mqttUser').value,mqtt_pass:$('mqttPass').value,
    ntp_server:$('ntpServer').value,ntp_tz:$('ntpTz').value};
}
function updateCmdModeUI(){
  const m=+$('cmdMode').value;
  ['mqttUri','mqttTopic','mqttUser','mqttPass'].forEach(id=>$(id).disabled=(m===0));
  ['urlHex','urlSendBtn'].forEach(id=>$(id).disabled=(m===1));
  $('cmdHint').textContent = m===0?t('cmdHint0'):(m===1?t('cmdHint1'):t('cmdHint2'));
  const path=location.pathname, code=hexClean($('urlHex').value), topic=$('mqttTopic').value||'pass-through-uart';
  const hex=encodeURIComponent(code||'41540D');
  $('urlExample').innerHTML=t('urlSendLabel')+'<code>'+location.origin+path+'?hex='+hex+'&amp;target='+$('cmdTarget').value+'</code><br>'+
    t('urlReadLabel')+'<code>'+location.origin+path+'?log=50</code><br>'+
    t('mqttCodeLabel')+'<code>'+topic+'/code ← '+(code||'41540D')+'</code><br>'+
    t('mqttTargetLabel')+'<code>'+topic+'/target</code><br>'+
    t('mqttOutLabel')+'<code>'+topic+'/tx/code</code> + <code>'+topic+'/tx/target</code><br>'+
    'MQTT: <code>'+topic+'/rx</code> → <code>'+topic+'/result</code>, '+t('trafficLabel')+' <code>'+topic+'/traffic</code>';
}
['cmdMode','cmdTarget'].forEach(id=>$(id).onchange=updateCmdModeUI);
['urlHex','mqttTopic'].forEach(id=>$(id).oninput=updateCmdModeUI);
$('cmdApply').onclick=async()=>{
  const j=await api('/api/config',configPayload());
  if(j&&j.ok)clearDirty(CFG_IDS.concat('mqttPass'));
  await refresh();
};
/* Каналы команд сохраняем сразу, как и карточка моста: несохранённые поля
   терялись бы при перезагрузке страницы/устройства. */
function channelsPayload(){
  return {cmd_mode:+$('cmdMode').value,cmd_target:+$('cmdTarget').value,
    mqtt_uri:$('mqttUri').value,mqtt_topic:$('mqttTopic').value,
    mqtt_user:$('mqttUser').value,mqtt_pass:$('mqttPass').value};
}
let chanTimer=null;
function saveChannels(){
  clearTimeout(chanTimer);
  chanTimer=setTimeout(async()=>{
    const ids=['cmdMode','cmdTarget','mqttUri','mqttTopic','mqttUser'];
    const j=await api('/api/config',channelsPayload());
    if(j&&j.ok)clearDirty(ids);
    await refresh();
  },400);
}
['cmdMode','cmdTarget','mqttUri','mqttTopic','mqttUser','mqttPass'].forEach(id=>$(id).addEventListener('change',saveChannels));
['mqttUri','mqttTopic','mqttUser'].forEach(id=>$(id).addEventListener('input',saveChannels));
$('ntpApply').onclick=async()=>{
  const j=await api('/api/config',{ntp_server:$('ntpServer').value,ntp_tz:$('ntpTz').value});
  if(j&&j.ok)clearDirty(['ntpServer','ntpTz']);
  await refresh();
};
$('urlSendBtn').onclick=()=>{
  window.open(location.origin+location.pathname+'?hex='+encodeURIComponent(hexClean($('urlHex').value))+'&target='+$('cmdTarget').value,'_blank');
};
/* Все элементы карточки «Сквозной мост UART» сохраняются сразу при изменении */
const BOOL_CFG=['bridge','echo','mirror','syslog','autoBaud1','autoBaud2'];
const BRIDGE_API={autoBaud1:'auto_baud1',autoBaud2:'auto_baud2',baudProbe:'baud_probe'};
['bridge','echo','mirror','syslog','autoBaud1','autoBaud2',
 'baud1','baud2','baudProbe'].forEach(id=>$(id).onchange=async()=>{
  const el=$(id), key=BRIDGE_API[id]||id;
  el.disabled=true;
  const val = BOOL_CFG.includes(id) ? el.checked : (id==='baudProbe' ? el.value : +el.value);
  const body = {[key]:val};
  await saveCfg([id],body);
  await refresh();
  el.disabled=false;
});
/* Поля «Отправка команды» тоже сохраняются сразу, иначе их перетирает refresh() */
const SEND_FIELD_API={usbTarget:'usb_target',autoMs:'auto_ms',autoText:'auto_text'};
['usbTarget','autoMs','autoText'].forEach(id=>$(id).onchange=async()=>{
  const el=$(id), val=(id==='autoText')?el.value:(+el.value||0);
  await saveCfg([id],{[SEND_FIELD_API[id]]:val});
  await refresh();
});
$('sendBtn').onclick=async()=>{
  const fmt=$('fmt').value;
  const data=(fmt==='hex')?hexClean($('cmd').value):$('cmd').value;
  if(!data)return;
  await api('/api/send',{target:+$('target').value,format:fmt,data:data});
};
$('cmd').addEventListener('keydown',e=>{if(e.key==='Enter'&&(e.ctrlKey||e.metaKey)){e.preventDefault();$('sendBtn').click();}});
document.querySelectorAll('[data-quick]').forEach(b=>b.onclick=()=>{$('cmd').value=b.dataset.quick;$('sendBtn').click();});
$('clearLog').onclick=async()=>{await api('/api/log/clear',{});LOG_RAW.length=0;$('log').innerHTML='';};
$('wifiBtn').onclick=async()=>{
  const j=await api('/api/config',{wifi_ssid:$('ssid').value,wifi_pass:$('wpass').value});
  if(j&&j.ok)clearDirty(['ssid']);
};

let aps=[];
function renderSsid(){
  const q=$('ssidSearch').value.toLowerCase().trim();
  const box=$('ssidList');box.innerHTML='';
  aps.filter(a=>!q||a.ssid.toLowerCase().includes(q)).forEach(a=>{
    const d=document.createElement('div');
    d.appendChild(document.createTextNode((a.auth?'🔒 ':'')+a.ssid+'  '));
    const s=document.createElement('span');s.textContent=a.rssi+' dBm · CH'+a.channel;
    d.appendChild(s);
    d.onclick=()=>{$('ssid').value=a.ssid;dirty.add('ssid');};
    box.appendChild(d);
  });
}
$('ssidSearch').oninput=renderSsid;
$('scanBtn').onclick=async()=>{
  const j=await api('/api/wifi/scan');
  if(j){aps=j.aps||[];renderSsid();toast(t('foundAps')+aps.length);}
};

$('passwdBtn').onclick=async()=>{
  const old=$('oldPass').value,nw=$('newPass').value,nw2=$('newPass2').value;
  if(!old||!nw||!nw2){toast(t('passwordMissing'),true);return;}
  if(nw!==nw2){toast(t('passwordMismatch'),true);return;}
  const j=await api('/api/passwd',{old:old,new:nw});
  if(j&&j.ok){$('oldPass').value='';$('newPass').value='';$('newPass2').value='';}
};
$('otaBtn').onclick=async()=>{
  const f=$('otaFile').files&&$('otaFile').files[0];
  if(!f){toast(t('fwFileMissing'),true);return;}
  toast(t('otaLoading'));
  try{
    const buf=await f.arrayBuffer();
    const r=await fetch('/api/ota/file',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:buf});
    if(r.status===401){location.href=location.pathname;return;}
    const j=await r.json();
    toast(trMsg(j.msg||(j.ok?'OTA запущена':'OTA ошибка')),!j.ok);
  }catch(e){toast(t('errLink'),true);}
};
$('checkOtaBtn').onclick=async()=>{
  const j=await api('/api/ota/check',{});
  if(j&&j.ok){toast(t('updateCheckStarted'));setTimeout(()=>refresh(true),1500);}
};
$('otaRemoteBtn').onclick=async()=>{await api('/api/ota',{});};
$('rebootBtn').onclick=()=>{if(confirm(t('rebootConfirm')))api('/api/reboot',{});};

$('cfgSaveBtn').onclick=async()=>{
  try{
    const r=await fetch('/api/config/export');
    if(r.status===401){location.href=location.pathname;return;}
    if(!r.ok){toast(t('exportErr'),true);return;}
    const blob=new Blob([await r.text()],{type:'application/json'});
    const a=document.createElement('a'), d=new Date(), p=n=>String(n).padStart(2,'0');
    a.href=URL.createObjectURL(blob);
    a.download='pass-through-uart-'+(($('ver').textContent||'1.0.5'))+'-'+d.getFullYear()+p(d.getMonth()+1)+p(d.getDate())+'.json';
    document.body.appendChild(a);a.click();a.remove();
    setTimeout(()=>URL.revokeObjectURL(a.href),1000);
    toast(t('cfgSavedFile'));
  }catch(e){toast(t('saveFileErr'),true);}
};
$('cfgLoadBtn').onclick=()=>$('cfgFile').click();
$('cfgFile').onchange=async()=>{
  const file=$('cfgFile').files[0];
  if(!file)return;
  if(!confirm(t('restoreConfirm',{f:file.name}))){
    $('cfgFile').value='';return;
  }
  try{
    const obj=JSON.parse(await file.text());
    const j=await api('/api/config/import',obj);
    if(j&&j.ok){dirty.clear();await refresh();}
  }catch(e){toast(t('readFileErr'),true);}
  finally{$('cfgFile').value='';}
};

applyLang();loadLog();wsConnect();setInterval(refresh,2500);$('scanBtn').click();
</script>
</body>
</html>)HTML";

extern "C" const char *web_ui_html(void)
{
    return INDEX_HTML;
}
