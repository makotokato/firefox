/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { useSelector } from "react-redux";
import { Lists } from "./Lists/Lists";
import { FocusTimer } from "./FocusTimer/FocusTimer";

const PREF_WIDGETS_LISTS_ENABLED = "widgets.lists.enabled";
const PREF_WIDGETS_SYSTEM_LISTS_ENABLED = "widgets.system.lists.enabled";
const PREF_WIDGETS_TIMER_ENABLED = "widgets.focusTimer.enabled";
const PREF_WIDGETS_SYSTEM_TIMER_ENABLED = "widgets.system.focusTimer.enabled";

function Widgets() {
  const prefs = useSelector(state => state.Prefs.values);

  const listsEnabled =
    prefs[PREF_WIDGETS_SYSTEM_LISTS_ENABLED] &&
    prefs[PREF_WIDGETS_LISTS_ENABLED];

  const timerEnabled =
    prefs[PREF_WIDGETS_SYSTEM_TIMER_ENABLED] &&
    prefs[PREF_WIDGETS_TIMER_ENABLED];

  return (
    <div className="widgets-container">
      {listsEnabled && <Lists />}
      {timerEnabled && <FocusTimer />}
    </div>
  );
}

export { Widgets };
