#include "WebAssets.h"

namespace web_assets {

const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>DG-LAB 控制器</title>
  <style>
    :root{color-scheme:dark;font-family:system-ui,sans-serif;background:#0f1217;color:#f5e7a7}
    *{box-sizing:border-box}body{margin:0}.app{max-width:720px;margin:auto;padding:16px}
    .card{background:#1a2028;border-radius:16px;padding:16px;margin-bottom:12px}
    .tabs{position:sticky;bottom:0;display:grid;grid-template-columns:repeat(3,1fr);gap:6px;background:#11161c;padding:8px;border-radius:14px}
    button{min-height:44px;border:0;border-radius:11px;background:#286fbd;color:#fff;font-weight:700}
    [hidden]{display:none!important}
  </style>
</head>
<body>
  <main class="app">
    <header class="card"><h1>DG-LAB</h1><p id="connection-label">正在读取状态</p></header>
    <section data-page="status"><div class="card"><h2>状态</h2><p id="status-placeholder">等待控制器状态</p></div></section>
    <section data-page="control" hidden><div class="card"><h2>控制</h2><p>连接设备后可使用控制功能。</p></div></section>
    <section data-page="logs" hidden><div class="card"><h2>日志</h2><div id="log-list"></div></div></section>
    <nav class="tabs" aria-label="页面">
      <button type="button" data-tab="status">状态</button>
      <button type="button" data-tab="control">控制</button>
      <button type="button" data-tab="logs">日志</button>
    </nav>
  </main>
</body>
</html>)HTML";

}  // namespace web_assets
