// Unit tests for the test-panel mark merge. Run: node --test test/
//
// Every case here was first checked by hand with curl against the deployed
// worker (2026-08-27). These tests exist so the next change to that code does
// not need the same hour, and so a regression shows up before it eats a real
// testing session.

import test from 'node:test';
import assert from 'node:assert/strict';
import { mergeState, MAX_NOTE } from '../src/state.mjs';

test('nauja zyma prisideda', () => {
  const out = mergeState({}, { 'T-19': { v: 'pass', n: '', t: 100 } });
  assert.deepEqual(out, { 'T-19': { v: 'pass', n: '', t: 100 } });
});

test('naujesne zyma nugali senesne', () => {
  const cur = { 'T-19': { v: 'pass', n: '', t: 100 } };
  const out = mergeState(cur, { 'T-19': { v: 'fail', n: 'kritо', t: 200 } });
  assert.equal(out['T-19'].v, 'fail');
});

test('senesne zyma NEnumusa naujesnes (vakar paliktas langas)', () => {
  const cur = { 'T-19': { v: 'pass', n: '', t: 200 } };
  const out = mergeState(cur, { 'T-19': { v: 'fail', n: '', t: 50 } });
  assert.equal(out['T-19'].v, 'pass', 'senas momentinis vaizdas neturi grizti');
});

test('vienodas laikas: laimi ateinantis', () => {
  const cur = { 'T-19': { v: 'pass', n: '', t: 100 } };
  const out = mergeState(cur, { 'T-19': { v: 'skip', n: '', t: 100 } });
  assert.equal(out['T-19'].v, 'skip');
});

test('svetimas raktas ignoruojamas, tikras priimamas', () => {
  const out = mergeState({}, {
    hack: { v: 'pass', t: 9 },
    '__proto__x': { v: 'pass', t: 9 },
    'T-5': { v: 'pass', t: 9 },
  });
  assert.deepEqual(Object.keys(out), ['T-5']);
});

test('nezinoma reiksme virsta tuscia, o eilute be pastabos dingsta', () => {
  const out = mergeState({ 'T-7': { v: 'pass', n: '', t: 1 } },
                         { 'T-7': { v: 'sugalvota', n: '', t: 2 } });
  assert.equal(out['T-7'], undefined);
});

test('isvalyta eilute pasalinama', () => {
  const cur = { 'S-1': { v: 'pass', n: 'buvo', t: 1 } };
  const out = mergeState(cur, { 'S-1': { v: null, n: '', t: 2 } });
  assert.deepEqual(out, {});
});

test('pastaba be varneles islieka', () => {
  const out = mergeState({}, { 'S-3': { v: null, n: 'tik pastaba', t: 5 } });
  assert.equal(out['S-3'].n, 'tik pastaba');
});

test('per ilga pastaba apkarpoma', () => {
  const out = mergeState({}, { 'S-3': { v: 'pass', n: 'x'.repeat(5000), t: 1 } });
  assert.equal(out['S-3'].n.length, MAX_NOTE);
});

test('siuksles vietoj eilutes ignoruojamos', () => {
  const cur = { 'T-9': { v: 'pass', n: '', t: 1 } };
  for (const junk of [null, 'tekstas', 42, ['masyvas']]) {
    assert.deepEqual(mergeState(cur, { 'T-9': junk }), cur);
  }
});

test('trukstamas laikas laikomas nuliu, o ne NaN', () => {
  const out = mergeState({}, { 'T-2': { v: 'pass' } });
  assert.equal(out['T-2'].t, 0);
});

test('esama busena nekeiciama vietoje', () => {
  const cur = { 'T-1': { v: 'pass', n: '', t: 1 } };
  const copy = JSON.parse(JSON.stringify(cur));
  mergeState(cur, { 'T-1': { v: 'fail', n: '', t: 9 } });
  assert.deepEqual(cur, copy, 'mergeState neturi liesti paduotos busenos');
});

test('bloga ateinanti reiksme grazina esama be pakeitimu', () => {
  const cur = { 'T-1': { v: 'pass', n: '', t: 1 } };
  for (const bad of [null, undefined, 'ne objektas', ['masyvas']]) {
    assert.deepEqual(mergeState(cur, bad), cur);
  }
});
