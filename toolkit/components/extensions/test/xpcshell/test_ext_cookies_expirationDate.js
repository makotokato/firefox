"use strict";

add_task(async function test_expirationDate_and_maxageCap() {
  async function backgroundScript() {
    const kOneDay = 60 * 60 * 24;
    const kTenDays = kOneDay * 10;
    const kFourHundredDays = kTenDays * 40;
    const kNow = Math.round(Date.now() / 1000);

    await browser.cookies.set({
      url: "https://example.com",
      name: "a",
      value: "b",
      // kFourHundredDays * 10 exceeds the limit from network.cookie.maxageCap
      expirationDate: kNow + kFourHundredDays * 10,
    });

    let cookie = await browser.cookies.getAll({
      url: "https://example.com",
      name: "a",
    });

    browser.test.assertEq(cookie.length, 1, "Cookie with 400-days expiry");
    // The extra day is just to avoid out-of-sync timing between parent and content processes.
    browser.test.assertTrue(
      cookie[0].expirationDate <= kNow + kFourHundredDays + kOneDay,
      "ExpirationDate is normalized to max 400 days"
    );

    await browser.cookies.set({
      url: "https://example.com",
      name: "a",
      value: "b",
      expirationDate: kNow + kTenDays,
    });

    cookie = await browser.cookies.getAll({
      url: "https://example.com",
      name: "a",
    });

    browser.test.assertEq(cookie.length, 1, "Cookie with 10-day expiry");
    browser.test.assertTrue(
      cookie[0].expirationDate <= kNow + kTenDays,
      "ExpirationDate is around 10 days"
    );

    browser.test.sendMessage("done");
  }

  const extension = ExtensionTestUtils.loadExtension({
    background: backgroundScript,
    manifest: {
      permissions: ["cookies", "https://example.com/*"],
    },
  });

  await extension.startup();
  await extension.awaitMessage("done");
  await extension.unload();
});

add_task(async function test_expirationDate_with_fraction() {
  async function backgroundScript() {
    const expirationDate = Math.floor(Date.now() / 1000) + 3600.5;
    let cookie = await browser.cookies.set({
      url: "https://example.com/",
      name: "expirationDate",
      value: "hello",
      expirationDate,
    });
    browser.test.assertEq(
      cookie.expirationDate,
      expirationDate,
      "Cookie with fractional expirationDate"
    );

    cookie = await browser.cookies.getAll({
      url: "https://example.com",
      name: "expirationDate",
    });
    browser.test.assertEq(
      cookie.length,
      1,
      "Cookie with fractional expirationDate found"
    );
    browser.test.assertEq(
      cookie[0].expirationDate,
      expirationDate,
      "Cookie with fractional expirationDate matches"
    );

    browser.test.sendMessage("done");
  }

  const extension = ExtensionTestUtils.loadExtension({
    background: backgroundScript,
    manifest: {
      permissions: ["cookies", "https://example.com/*"],
    },
  });

  await extension.startup();
  await extension.awaitMessage("done");
  await extension.unload();
});
