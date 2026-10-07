const { test, expect } = require('@playwright/test');
const path = require('path');
const manager = {
  busy: false,
  authenticated: false,
  credentials_ready: false,
  autorun: false,
  status: 'Enter HU credentials to connect',
  hu_state: 'inactive',
  live_connected: false,
  cards: [],
  log: '',
  progress: null,
};
async function fixture(page) {
  const calls = [];
  let opened = false;
  await page.route('**/status', (route) =>
    route.fulfill({ json: { usb_network_up: true, board: 'ESP32-S3' } }),
  );
  await page.route('**/manage/status', (route) => route.fulfill({ json: manager }));
  await page.route('**/manage/action', async (route) => {
    calls.push(route.request());
    await route.fulfill({ body: 'OK' });
  });
  await page.route('**/api/settings', async (route) => {
    if (route.request().method() === 'POST') {
      calls.push(route.request());
      await route.fulfill({ body: 'Saved' });
    } else
      await route.fulfill({
        json: {
          ssid: 'MIB-Link',
          forwards: [
            [2323, 23],
            [2222, 22],
            [8080, 80],
          ],
          listeners: 3,
          active: 0,
        },
      });
  });
  await page.route('**/api/console', async (route) => {
    const req = route.request();
    if (req.method() === 'POST') {
      calls.push(req);
      if (req.headers()['x-mib-action'] === 'open') opened = true;
      if (req.headers()['x-mib-action'] === 'close') opened = false;
      await route.fulfill({ body: 'OK' });
    } else
      await route.fulfill({
        json: {
          session: opened ? 1 : 0,
          connected: opened,
          ready: opened,
          status: opened ? 'Connected' : 'Disconnected',
          next: opened ? 7 : 0,
          lost: false,
          data: opened && req.headers()['x-mib-cursor'] === '0' ? '6c6f67696e3a20' : '',
        },
      });
  });
  await page.goto('/');
  await expect(page.locator('#connection')).toHaveText('USB connected');
  return calls;
}
test('workspace layout, keyboard navigation, login and no external dependencies', async ({
  page,
}) => {
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.setViewportSize({ width: 1280, height: 1120 });
  const calls = await fixture(page);
  await expect(page.getByRole('heading', { name: 'Head-unit connection' })).toBeVisible();
  await expect(page.locator('#platform')).toHaveText('ESP32-S3 / USB Ethernet');
  await expect(page.locator('#run')).toBeDisabled();
  await page.screenshot({ path: path.join(__dirname, '../../docs/overview.png'), fullPage: true });
  await page.locator('#username').fill('test-user');
  await page.locator('#password').fill('fixture-password');
  await page.getByRole('button', { name: 'Connect & scan' }).click();
  await expect.poll(() => calls.length).toBe(1);
  expect(calls[0].headers()['x-hu-password']).toBe('fixture-password');
  await expect(page.locator('#password')).toHaveValue('');
  expect(errors).toEqual([]);
});
test('editable Wi-Fi and arbitrary TCP mappings', async ({ page }) => {
  const calls = await fixture(page);
  await page.getByRole('button', { name: 'Wi-Fi & services' }).click();
  await expect(page.locator('#ssid')).toHaveValue('MIB-Link');
  await page.locator('#ssid').fill('My MIB');
  await page.locator('#wifi-password').fill('newlink1');
  await page.locator('#local-0').fill('6000');
  await page.locator('#remote-0').fill('12345');
  await page.getByRole('button', { name: 'Save & restart device' }).click();
  await expect(page.locator('#notice')).toContainText('Settings saved');
  expect(calls.at(-1).headers()['x-mib-forward1']).toBe('6000:12345');
  expect(calls.at(-1).headers()['x-mib-ssid']).toBe('My MIB');
  await expect(page.locator('#wifi-password')).toHaveValue('');
});
test('console prompts, password masking and Ctrl+C', async ({ page }) => {
  const calls = await fixture(page);
  await page.getByRole('button', { name: 'Console', exact: true }).click();
  await page.getByRole('button', { name: 'Open console' }).click();
  await expect(page.locator('#terminal')).toContainText('login:');
  await page.locator('#secret-input').check();
  await expect(page.locator('#command')).toHaveAttribute('type', 'password');
  await page.locator('#command').fill('secret');
  await page.getByRole('button', { name: 'Send ↵' }).click();
  await expect(page.locator('#command')).toHaveValue('');
  expect(calls.at(-1).headers()['x-mib-data']).toBe('7365637265740d0a');
  await page.getByRole('button', { name: 'Ctrl+C', exact: true }).click();
  expect(calls.at(-1).headers()['x-mib-data']).toBe('03');
  await page.getByRole('button', { name: 'Disconnect', exact: true }).click();
  await expect(page.locator('#console-state')).toHaveText('Disconnected');
});
test('mobile layout stays within viewport', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await fixture(page);
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(390);
  for (const name of ['Console', 'Wi-Fi & services']) {
    await page.getByRole('button', { name, exact: true }).click();
    expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(390);
  }
  await page.getByRole('button', { name: 'Overview', exact: true }).click();
  await page.screenshot({ path: path.join(__dirname, '../../docs/mobile.png'), fullPage: true });
});
test('identical bundles on different SD slots remain individually selectable', async ({ page }) => {
  const calls = await fixture(page);
  const digest = 'a'.repeat(64),
    cards = ['sda0', 'sdb0'].map((slot) => ({
      slot,
      digest,
      name: 'Same bundle',
      compatible: true,
    }));
  await page.route('**/manage/status', (route) =>
    route.fulfill({ json: { ...manager, authenticated: true, cards } }),
  );
  await page.reload();
  await expect(page.locator('#bundle option')).toHaveCount(2);
  await page.locator('#bundle').selectOption(`sdb0:${digest}`);
  await page.getByRole('button', { name: 'Run payload', exact: true }).click();
  await expect.poll(() => calls.length).toBe(1);
  expect(calls[0].headers()['x-mhi2-slot']).toBe('sdb0');
});
