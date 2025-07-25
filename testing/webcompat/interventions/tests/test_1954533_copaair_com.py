import pytest

URL = "https://www.copaair.com/en-gs/enrollment/"

CAPTCHA_CSS = "iframe[src*='Incapsula_Resource']"
NAME_CSS = "#input-name"
SURNAME_CSS = "#input-lastName"
YEAR_SELECTOR_CSS = "#year"
YEAR_CSS = "#year-open [data-value='1978']"
MONTH_SELECTOR_CSS = "#month"
MONTH_CSS = "#month-open [data-value='3']"
DAY_SELECTOR_CSS = "#day"
DAY_CSS = "#day-open [data-value='6']"
COUNTRY_SELECTOR_CSS = "#option"
COUNTRY_CSS = "#option-open [data-value='CA'], #option-open [data-value='0']"
DOC_SELECTOR_CSS = "#memberDoc"
DOC_CSS = "#memberDoc-open [data-value='p']"
DOC_NUMBER_CSS = "[id='input-memberDoc.documentId']"
DOC_COUNTRY_SELECTOR_CSS = "[id='memberDoc.issuingCountry']"
DOC_COUNTRY_CSS = "[id='memberDoc.issuingCountry-open'] [data-value='CA'], [id='memberDoc.issuingCountry-open'] [data-value='0']"
DOC_YEAR_SELECTOR_CSS = "div:has(+ input[name='memberDoc.validUntil.year'])"
DOC_YEAR_CSS = "#year-open [data-value='2035']"
DOC_MONTH_SELECTOR_CSS = "div:has(+ input[name='memberDoc.validUntil.month'])"
DOC_MONTH_CSS = "#month-open [data-value='7']"
DOC_DAY_SELECTOR_CSS = "div:has(+ input[name='memberDoc.validUntil.day'])"
DOC_DAY_CSS = "#day-open [data-value='12']"
EMAIL_CSS = "#input-email"
PASSWORD_CSS = "#password-input-password"
CREATE_BUTTON_TEXT = "Create an account"
ACCEPT_BUTTON_TEXT = "Accept"
OOPS_CSS = "img[alt='Connectmiles Logo']"
DIALOG_BUTTONS_CSS = "#contentContainerModal+.MuiBox-root button"
FAIL_MSG = """TypeError: can't access property "innerText", document.querySelector(...) is null"""


async def get_accept_button(client, in_headless_mode):
    await client.make_preload_script("delete navigator.__proto__.webdriver")
    await client.make_preload_script(
        "Object.defineProperty(window, 'y', {get: () => document.head, configurable:true})"
    )
    await client.navigate(URL, wait="none")
    captcha, name = client.await_first_element_of(
        [
            client.css(CAPTCHA_CSS),
            client.css(NAME_CSS),
        ],
        is_displayed=True,
    )
    if captcha:
        if in_headless_mode:
            pytest.xfail("Captcha cannot be done in headless mode")
            return
        print(
            "Please do Captcha...\a\n"
        )  # beep to let the user know to do the reCAPTCHA
    client.await_css(NAME_CSS, is_displayed=True).send_keys("webcompat")
    client.await_css(SURNAME_CSS, is_displayed=True).send_keys("tester")
    client.click(client.await_css(YEAR_SELECTOR_CSS, is_displayed=True), force=True)
    client.click(client.await_css(YEAR_CSS, is_displayed=True), force=True)
    client.click(client.await_css(MONTH_SELECTOR_CSS, is_displayed=True), force=True)
    client.click(client.await_css(MONTH_CSS, is_displayed=True), force=True)
    client.click(client.await_css(DAY_SELECTOR_CSS, is_displayed=True), force=True)
    client.click(client.await_css(DAY_CSS, is_displayed=True), force=True)
    client.click(client.await_css(COUNTRY_SELECTOR_CSS, is_displayed=True), force=True)
    client.click(client.await_css(COUNTRY_CSS, is_displayed=True), force=True)
    client.click(client.await_css(DOC_SELECTOR_CSS, is_displayed=True), force=True)
    client.click(client.await_css(DOC_CSS, is_displayed=True), force=True)
    client.await_css(DOC_NUMBER_CSS, is_displayed=True).send_keys("1235123")
    client.click(
        client.await_css(DOC_COUNTRY_SELECTOR_CSS, is_displayed=True), force=True
    )
    client.click(client.await_css(DOC_COUNTRY_CSS, is_displayed=True), force=True)
    client.click(client.await_css(DOC_YEAR_SELECTOR_CSS, is_displayed=True), force=True)
    client.click(client.await_css(DOC_YEAR_CSS, is_displayed=True), force=True)
    client.click(
        client.await_css(DOC_MONTH_SELECTOR_CSS, is_displayed=True), force=True
    )
    client.click(client.await_css(DOC_MONTH_CSS, is_displayed=True), force=True)
    client.click(client.await_css(DOC_DAY_SELECTOR_CSS, is_displayed=True), force=True)
    client.click(client.await_css(DOC_DAY_CSS, is_displayed=True), force=True)
    client.await_css(EMAIL_CSS, is_displayed=True).send_keys(
        "webcompatTester@gmail.com"
    )
    client.await_css(PASSWORD_CSS, is_displayed=True).send_keys("A1b2_xxx")
    create = client.await_text(CREATE_BUTTON_TEXT, is_displayed=True)
    client.execute_async_script(
        """
      const [btn, done] = arguments;
      timer = setInterval(() => {
        if (!btn.hasAttribute("disabled")) {
          clearInterval(timer);
          done();
        }
      }, 100);
    """,
        create,
    )
    client.click(create, force=True)
    return client.await_css(DIALOG_BUTTONS_CSS, all=True, is_displayed=True)[1]


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_regression(client, in_headless_mode):
    await get_accept_button(client, in_headless_mode)
    client.await_css(
        "button",
        condition=f"elem.innerText.includes('{ACCEPT_BUTTON_TEXT}')",
        is_displayed=True,
    ).click()
    assert client.await_css(OOPS_CSS, is_displayed=True)
