/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export const FormValidationUtils = {
  /**
   * Return the validation information that is found at first from invalid
   * elements
   *
   * @Param invalidElements            The array of invalid elements
   * @Param validateElementsForWindow  The current window
   */
  getFirstValidationInformation(invalidElements, validateElementsForWindow) {
    // Show a validation message on the first focusable element.
    for (let element of invalidElements) {
      // Insure that this is the FormSubmitObserver associated with the
      // element / window this notification is about.
      if (
        validateElementsForWindow != element.ownerGlobal.document.defaultView
      ) {
        return null;
      }

      if (
        !(
          ChromeUtils.getClassName(element) === "HTMLInputElement" ||
          ChromeUtils.getClassName(element) === "HTMLTextAreaElement" ||
          ChromeUtils.getClassName(element) === "HTMLSelectElement" ||
          ChromeUtils.getClassName(element) === "HTMLButtonElement" ||
          element.isFormAssociatedCustomElement
        )
      ) {
        continue;
      }

      const validationMessage = element.isFormAssociatedCustomElement
        ? element.internals.validationMessage
        : element.validationMessage;

      if (element.isFormAssociatedCustomElement) {
        // For element that are form-associated custom elements, user agents
        // should use their validation anchor instead.
        // It is not clear how constraint validation should work for FACE in
        // spec if the validation anchor is null, see
        // https://github.com/whatwg/html/issues/10155. Blink seems fallback to
        // FACE itself when validation anchor is null, which looks reasonable.
        element = element.internals.validationAnchor || element;
      }

      if (!element || !Services.focus.elementIsFocusable(element, 0)) {
        continue;
      }

      return { element, validationMessage };
    }
    return null;
  },
};
