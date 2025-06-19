/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";
import { GeckoViewActorChild } from "resource://gre/modules/GeckoViewActorChild.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FormValidationUtils: "resource://gre/modules/FormValidationUtils.sys.mjs",
  GeckoViewFormValidation:
    "resource://gre/modules/GeckoViewFormValidation.sys.mjs",
});

const { debug, warn } = GeckoViewUtils.initLogging(
  "GeckoViewFormValidationChild"
);

export class GeckoViewFormValidationChild extends GeckoViewActorChild {
  constructor() {
    super();
    this._validationMessage = "";
    this._element = null;
  }

  handleEvent(aEvent) {
    const { type } = aEvent;
    debug`handleEvent: ${type}`;

    switch (aEvent.type) {
      case "MozInvalidForm":
        aEvent.preventDefault();
        this._notifyInvalidSubmit(aEvent.detail);
        break;
      case "pageshow":
        if (this._isRootDocumentEvent(aEvent)) {
          this._hidePopup();
        }
        break;
      case "pagehide":
        // Act as if the element is being blurred. This will remove any
        // listeners and hide the popup.
        this._onBlur();
        break;
      case "input":
        this._onInput(aEvent);
        break;
      case "blur":
        this._onBlur();
        break;
      case "mozvisualscroll":
        if (this._element) {
          this._onBlur();
        }
        break;
    }
  }

  async _notifyInvalidSubmit(aInvalidElements) {
    // Show a validation message on the first focusable element.
    const info = lazy.FormValidationUtils.getFirstValidationInformation(
      aInvalidElements,
      this.contentWindow
    );
    if (!info) {
      return;
    }
    const { element, validationMessage } = info;

    // Update validation message before showing notification
    this._validationMessage = validationMessage;

    // Don't connect up to the same element more than once.
    if (this._element == element) {
      this._showPopup(element);
      return;
    }

    this._onBlur();
    element.focus();
    element.scrollIntoView({ block: "center" });

    // We should wait for ending scroll. The popup for validation should be
    // shown at valid postion.

    await GeckoViewUtils.waitForPanZoomState(this.contentWindow);
    this._showPopup(element);

    this._element = element;

    // Watch for input changes which may change the validation message.
    this._element.addEventListener("input", this);

    // Watch for focus changes so we can disconnect our listeners and
    // hide the popup.
    this._element.addEventListener("blur", this);
  }

  _onInput(aEvent) {
    const element = aEvent.originalTarget;

    // If the form input is now valid, hide the popup.
    if (element.validity.valid) {
      this._hidePopup();
      return;
    }

    // If the element is still invalid for a new reason, we should update
    // the popup error message.
    if (this._validationMessage != element.validationMessage) {
      this._validationMessage = element.validationMessage;
      this._showPopup(element);
    }
  }

  _onBlur() {
    if (this._element) {
      this._element.removeEventListener("input", this);
      this._element.removeEventListener("blur", this);
    }
    this._hidePopup();
    this._element = null;
  }

  _showPopup(aElement) {
    lazy.GeckoViewFormValidation.onShowFormValidationPrompt(aElement);

    aElement.ownerGlobal.addEventListener("pagehide", this, {
      mozSystemGroup: true,
    });
  }

  _hidePopup() {
    lazy.GeckoViewFormValidation.onDismissFormInvalidPrompt();

    this._element?.ownerGlobal.removeEventListener("pagehide", this, {
      mozSystemGroup: true,
    });
  }

  _isRootDocumentEvent(aEvent) {
    if (this.contentWindow == null) {
      return true;
    }
    let target = aEvent.originalTarget;
    return (
      target == this.document ||
      (target.ownerDocument && target.ownerDocument == this.document)
    );
  }
}
