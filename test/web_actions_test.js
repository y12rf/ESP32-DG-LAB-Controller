const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

// Execute the shipped action handler, with controllable network completion.
const source = fs.readFileSync(path.join(__dirname, '../src/WebAssets.cpp'), 'utf8');
const action = source.slice(source.indexOf('  async function postAction('),
                            source.indexOf('\n  function switchTab('));

function harness() {
  const requests = [], messages = [];
  let refreshes = 0;
  const context = vm.createContext({
    state: { actionInFlight: false }, pendingStop: null,
    form: data => new URLSearchParams(data),
    request: (url, options) => new Promise((resolve, reject) => {
      requests.push({ url, body: options.body.toString(), resolve, reject });
    }),
    setText: (_, text) => messages.push(text),
    errorText: error => error.message,
    refreshStatus: async () => { refreshes++; },
  });
  vm.runInContext(action, context);
  return { context, requests, messages, refreshes: () => refreshes };
}

const flush = () => new Promise(resolve => setImmediate(resolve));

test('disconnect exposes cancellation and cancel sends an explicit stop', async () => {
  const h = harness(), nodes = new Map();
  const byId = id => {
    if (!nodes.has(id)) nodes.set(id, {hidden: false, addEventListener(_, fn) {this.click=fn;}});
    return nodes.get(id);
  };
  h.context.byId = byId;
  h.context.document = {querySelector: () => ({}), querySelectorAll: () => []};
  h.context.refreshDevices = () => {};
  vm.runInContext(source.slice(source.indexOf('  function renderStatus('),
                              source.indexOf('  async function refreshStatus(')), h.context);
  const cancelListener = source.split('\n').find(line => line.includes("byId('cancel-resume-button').addEventListener"));
  vm.runInContext(cancelListener, h.context);
  const status = {connected:false,ready:false,desiredSending:true,scanRevision:1};
  h.context.renderStatus(status);
  assert.equal(byId('control-content').hidden, true);
  assert.equal(byId('resume-output').hidden, false);
  const button=byId('cancel-resume-button');
  const action=button.click({currentTarget:button});
  assert.equal(h.requests[0].url, '/api/output');
  assert.equal(h.requests[0].body, 'sending=0');
  h.requests[0].resolve({ok:true});
  await action;
  h.context.renderStatus({...status,desiredSending:false});
  assert.equal(byId('resume-output').hidden, true);
});

test('errors and resume cancellation live outside hidden pages', () => {
  const shell=source.slice(0,source.indexOf('<section data-page='));
  assert.match(shell, /id="action-message"/);
  assert.match(shell, /id="cancel-resume-button"/);
  assert.equal((source.match(/id="action-message"/g)||[]).length, 1);
});

test('connection errors remain available to the global status message', async () => {
  const h=harness();
  const action=h.context.postAction('/api/connect',{address:'device',type:3},{});
  h.requests[0].reject(new Error('ble_failure'));
  await action;
  assert.equal(h.messages.at(-1),'ble_failure');
});

for (const failFirst of [false, true]) {
  test(`stop follows an in-flight action (${failFirst ? 'failure' : 'success'})`, async () => {
    const h = harness(), strengthButton = {}, stopButton = {};
    const action = h.context.postAction('/api/strength', {value: 5}, strengthButton);
    await h.context.postAction('/api/output', {sending: 0}, stopButton);
    assert.equal(h.requests.length, 1);
    assert.equal(stopButton.disabled, true);
    if (failFirst) h.requests[0].reject(new Error('network failure'));
    else h.requests[0].resolve({ok: true});
    await flush();
    assert.equal(h.requests.length, 2);
    assert.equal(h.requests[1].url, '/api/output');
    assert.equal(h.requests[1].body, 'sending=0');
    assert.equal(h.refreshes(), 0, 'stop must precede the status refresh');
    h.requests[1].resolve({ok: true});
    await action;
    assert.equal(strengthButton.disabled, false);
    assert.equal(stopButton.disabled, false);
    assert.equal(h.context.state.actionInFlight, false);
    assert.equal(h.refreshes(), 1);
  });
}

test('busy non-stop actions report that they were not accepted', async () => {
  const h = harness();
  const first = h.context.postAction('/api/strength', {value: 5}, {});
  await h.context.postAction('/api/wave', {type: 'b'}, {});
  assert.equal(h.requests.length, 1);
  assert.match(h.messages.at(-1), /稍后重试/);
  h.requests[0].resolve({ok: true});
  await first;
});

test('repeated pending stop clicks are coalesced and a failed stop unlocks UI', async () => {
  const h = harness(), button = {};
  const first = h.context.postAction('/api/strength', {value: 5}, {});
  await h.context.postAction('/api/output', {sending: 0}, button);
  await h.context.postAction('/api/output', {sending: 0}, button);
  h.requests[0].resolve({ok: true});
  await flush();
  assert.equal(h.requests.length, 2);
  h.requests[1].reject(new Error('stop failed'));
  await first;
  assert.equal(h.requests.length, 2, 'do not retry an uncertain command');
  assert.equal(button.disabled, false);
  assert.equal(h.context.state.actionInFlight, false);
  assert.equal(h.messages.at(-1), 'stop failed');
});
