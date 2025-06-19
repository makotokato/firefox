/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  GeckoViewPrompter: "resource://gre/modules/GeckoViewPrompter.sys.mjs",
  LayoutUtils: "resource://gre/modules/LayoutUtils.sys.mjs",
});

const { debug, warn } = GeckoViewUtils.initLogging("GeckoViewFormValidation");

export const GeckoViewFormValidation = {
  _prompt: null,

  async onShowFormValidationPrompt(aElement, contentWindow) {
    this._prompt?.dismiss();
    this._prompt = null;

    const prompt = new lazy.GeckoViewPrompter(
      aElement.ownerGlobal,
      contentWindow
    );
    // Match ValidityStateType in dom/html/nsIConstraintValidation.h
    let validityState = 0;
    if (aElement.validity.valueMissing) {
      validityState |= 1 << 0;
    }
    if (aElement.validity.typeMismatch) {
      validityState |= 1 << 1;
    }
    if (aElement.validity.patternMismatch) {
      validityState |= 1 << 2;
    }
    if (aElement.validity.tooLong) {
      validityState |= 1 << 3;
    }
    if (aElement.validity.tooShort) {
      validityState |= 1 << 4;
    }
    if (aElement.validity.rangeUnderflow) {
      validityState |= 1 << 5;
    }
    if (aElement.validity.rangeOverflow) {
      validityState |= 1 << 6;
    }
    if (aElement.validity.stepMismatch) {
      validityState |= 1 << 7;
    }
    if (aElement.validity.badInput) {
      validityState |= 1 << 8;
    }
    if (aElement.validity.customError) {
      validityState |= 1 << 9;
    }
    const screenRect = lazy.LayoutUtils.rectToScreenRect(
      aElement.ownerGlobal,
      aElement.getBoundingClientRect()
    );
    await prompt.asyncShowPrompt(
      {
        type: "formValidation",
        screenRect: {
          left: screenRect.left,
          top: screenRect.top,
          right: screenRect.right,
          bottom: screenRect.bottom,
        },
        message: aElement.validationMessage,
        validityState,
      },
      result => {}
    );
    this._prompt = prompt;
  },

  onDismissFormInvalidPrompt() {
    this._prompt?.dismiss();
    this._prompt = null;
  },
};
