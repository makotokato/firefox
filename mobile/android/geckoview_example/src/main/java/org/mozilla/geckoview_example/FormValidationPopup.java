/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview_example;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Matrix;
import android.graphics.RectF;
import android.graphics.drawable.BitmapDrawable;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.PopupWindow;
import android.widget.RelativeLayout.LayoutParams;
import android.widget.TextView;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.geckoview.GeckoResult;
import org.mozilla.geckoview.GeckoSession;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.BasePrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.FormValidationPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.PromptInstanceDelegate;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.PromptResponse;
import org.mozilla.geckoview_example.R;

final class FormValidationPopup {
  /**
   * Show form validation message pop up. This pop up is shown at the bottom of focused element that
   * has a validation error.
   */
  @SuppressLint("InflateParams")
  /* package */ static GeckoResult<PromptResponse> onFormValidationPrompt(
      final GeckoSession session, final FormValidationPrompt prompt) {
    final Context context = GeckoAppShell.getApplicationContext();
    final View parent = session.getTextInput().getView();
    if (parent == null) {
      return null;
    }

    final RectF bounds = new RectF();
    final Matrix matrix = new Matrix();
    session.getClientBounds(bounds);
    session.getClientToScreenMatrix(matrix);
    matrix.mapRect(bounds);

    final PopupWindow validationPopup = new PopupWindow(context);
    final LayoutInflater inflater =
        (LayoutInflater) context.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
    final View popupView = inflater.inflate(R.layout.validation_popup, null);
    if (popupView == null) {
      return null;
    }

    final TextView message = popupView.findViewById(R.id.validation_message_text);
    message.setText(prompt.message);

    validationPopup.setContentView(popupView);
    validationPopup.setFocusable(false);
    validationPopup.setTouchable(false);
    validationPopup.setBackgroundDrawable(new BitmapDrawable());

    // geckoview_validation_message height is 50dp.
    final int popupHeight = Math.round(50.0f * context.getResources().getDisplayMetrics().density);

    if (prompt.screenRect.top - popupHeight >= 0
        && prompt.screenRect.bottom + popupHeight > Math.round(bounds.bottom)) {
      // We don't have enough space in bottom to show the message. So we should show the pop up
      // above the target.
      final LayoutParams textLayout = new LayoutParams(message.getLayoutParams());
      textLayout.setMargins(0, 0, 0, 0);
      message.setLayoutParams(textLayout);
      popupView.findViewById(R.id.validation_message_arrow).setVisibility(View.GONE);
      popupView.findViewById(R.id.validation_message_arrow_inverted).setVisibility(View.VISIBLE);
      validationPopup.showAtLocation(
          parent,
          Gravity.TOP | Gravity.START,
          prompt.screenRect.left,
          prompt.screenRect.top - popupHeight);
    } else {
      validationPopup.showAtLocation(
          parent, Gravity.TOP | Gravity.START, prompt.screenRect.left, prompt.screenRect.bottom);
    }

    final GeckoResult<PromptResponse> response = new GeckoResult<PromptResponse>();
    prompt.setDelegate(
        new PromptInstanceDelegate() {
          @Override
          public void onPromptDismiss(final BasePrompt prompt) {
            // Content requests pop up is dismissed now.
            if (validationPopup != null && validationPopup.isShowing()) {
              validationPopup.dismiss();
            }
          }
        });
    // Validation pop up isn't dismissed by myself. It is dismissed by content.
    return response;
  }
}
