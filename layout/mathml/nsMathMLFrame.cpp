/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLFrame.h"

#include "gfxContext.h"
#include "gfxMathTable.h"
#include "gfxUtils.h"
#include "mozilla/dom/MathMLElement.h"
#include "mozilla/gfx/2D.h"
#include "nsCSSPseudoElements.h"
#include "nsCSSValue.h"
#include "nsLayoutUtils.h"
#include "nsMathMLChar.h"
#include "nsNameSpaceManager.h"
#include "nsPresContextInlines.h"

// used for parsing CSS units
#include "mozilla/dom/SVGAnimatedLength.h"
#include "mozilla/dom/SVGLength.h"

// used to map attributes into CSS rules
#include "mozilla/ServoStyleSet.h"
#include "nsDisplayList.h"

using namespace mozilla;
using namespace mozilla::gfx;

eMathMLFrameType nsMathMLFrame::GetMathMLFrameType() {
  // see if it is an embellished operator (mapped to 'Op' in TeX)
  if (mEmbellishData.coreFrame) {
    return GetMathMLFrameTypeFor(mEmbellishData.coreFrame);
  }

  // if it has a prescribed base, fetch the type from there
  if (mPresentationData.baseFrame) {
    return GetMathMLFrameTypeFor(mPresentationData.baseFrame);
  }

  // everything else is treated as ordinary (mapped to 'Ord' in TeX)
  return eMathMLFrameType_Ordinary;
}

NS_IMETHODIMP
nsMathMLFrame::InheritAutomaticData(nsIFrame* aParent) {
  mEmbellishData.flags = 0;
  mEmbellishData.coreFrame = nullptr;
  mEmbellishData.direction = NS_STRETCH_DIRECTION_UNSUPPORTED;
  mEmbellishData.leadingSpace = 0;
  mEmbellishData.trailingSpace = 0;

  mPresentationData.flags = 0;
  mPresentationData.baseFrame = nullptr;

  // by default, just inherit the display of our parent
  nsPresentationData parentData;
  GetPresentationDataFrom(aParent, parentData);

  return NS_OK;
}

NS_IMETHODIMP
nsMathMLFrame::UpdatePresentationData(uint32_t aFlagsValues,
                                      uint32_t aWhichFlags) {
  NS_ASSERTION(NS_MATHML_IS_COMPRESSED(aWhichFlags) ||
                   NS_MATHML_IS_DTLS_SET(aWhichFlags),
               "aWhichFlags should only be compression or dtls flag");

  if (NS_MATHML_IS_COMPRESSED(aWhichFlags)) {
    // updating the compression flag is allowed
    if (NS_MATHML_IS_COMPRESSED(aFlagsValues)) {
      // 'compressed' means 'prime' style in App. G, TeXbook
      mPresentationData.flags |= NS_MATHML_COMPRESSED;
    }
    // no else. the flag is sticky. it retains its value once it is set
  }
  // These flags determine whether the dtls font feature settings should
  // be applied.
  if (NS_MATHML_IS_DTLS_SET(aWhichFlags)) {
    if (NS_MATHML_IS_DTLS_SET(aFlagsValues)) {
      mPresentationData.flags |= NS_MATHML_DTLS;
    } else {
      mPresentationData.flags &= ~NS_MATHML_DTLS;
    }
  }
  return NS_OK;
}

/* static */
void nsMathMLFrame::GetEmbellishDataFrom(nsIFrame* aFrame,
                                         nsEmbellishData& aEmbellishData) {
  // initialize OUT params
  aEmbellishData.flags = 0;
  aEmbellishData.coreFrame = nullptr;
  aEmbellishData.direction = NS_STRETCH_DIRECTION_UNSUPPORTED;
  aEmbellishData.leadingSpace = 0;
  aEmbellishData.trailingSpace = 0;

  if (aFrame && aFrame->IsMathMLFrame()) {
    nsIMathMLFrame* mathMLFrame = do_QueryFrame(aFrame);
    if (mathMLFrame) {
      mathMLFrame->GetEmbellishData(aEmbellishData);
    }
  }
}

// helper to get the presentation data of a frame, by possibly walking up
// the frame hierarchy if we happen to be surrounded by non-MathML frames.
/* static */
void nsMathMLFrame::GetPresentationDataFrom(
    nsIFrame* aFrame, nsPresentationData& aPresentationData, bool aClimbTree) {
  // initialize OUT params
  aPresentationData.flags = 0;
  aPresentationData.baseFrame = nullptr;

  nsIFrame* frame = aFrame;
  while (frame) {
    if (frame->IsMathMLFrame()) {
      nsIMathMLFrame* mathMLFrame = do_QueryFrame(frame);
      if (mathMLFrame) {
        mathMLFrame->GetPresentationData(aPresentationData);
        break;
      }
    }
    // stop if the caller doesn't want to lookup beyond the frame
    if (!aClimbTree) {
      break;
    }
    // stop if we reach the root <math> tag
    nsIContent* content = frame->GetContent();
    NS_ASSERTION(content || !frame->GetParent(),  // no assert for the root
                 "dangling frame without a content node");
    if (!content) {
      break;
    }

    if (content->IsMathMLElement(nsGkAtoms::math)) {
      break;
    }
    frame = frame->GetParent();
  }
  NS_WARNING_ASSERTION(
      frame && frame->GetContent(),
      "bad MathML markup - could not find the top <math> element");
}

/* static */
void nsMathMLFrame::GetRuleThickness(DrawTarget* aDrawTarget,
                                     nsFontMetrics* aFontMetrics,
                                     nscoord& aRuleThickness) {
  nscoord xHeight = aFontMetrics->XHeight();
  char16_t overBar = 0x00AF;
  nsBoundingMetrics bm = nsLayoutUtils::AppUnitBoundsOfString(
      &overBar, 1, *aFontMetrics, aDrawTarget);
  aRuleThickness = bm.ascent + bm.descent;
  if (aRuleThickness <= 0 || aRuleThickness >= xHeight) {
    // fall-back to the other version
    GetRuleThickness(aFontMetrics, aRuleThickness);
  }
}

/* static */
void nsMathMLFrame::GetAxisHeight(DrawTarget* aDrawTarget,
                                  nsFontMetrics* aFontMetrics,
                                  nscoord& aAxisHeight) {
  RefPtr<gfxFont> mathFont =
      aFontMetrics->GetThebesFontGroup()->GetFirstMathFont();
  if (mathFont) {
    aAxisHeight = mathFont->MathTable()->Constant(
        gfxMathTable::AxisHeight, aFontMetrics->AppUnitsPerDevPixel());
    return;
  }

  nscoord xHeight = aFontMetrics->XHeight();
  char16_t minus = 0x2212;  // not '-', but official Unicode minus sign
  nsBoundingMetrics bm = nsLayoutUtils::AppUnitBoundsOfString(
      &minus, 1, *aFontMetrics, aDrawTarget);
  aAxisHeight = bm.ascent - (bm.ascent + bm.descent) / 2;
  if (aAxisHeight <= 0 || aAxisHeight >= xHeight) {
    // fall-back to the other version
    GetAxisHeight(aFontMetrics, aAxisHeight);
  }
}

/* static */
nscoord nsMathMLFrame::CalcLength(const nsCSSValue& aCSSValue,
                                  float aFontSizeInflation, nsIFrame* aFrame) {
  NS_ASSERTION(aCSSValue.IsLengthUnit(), "not a length unit");

  nsCSSUnit unit = aCSSValue.GetUnit();
  mozilla::dom::NonSVGFrameUserSpaceMetrics userSpaceMetrics(aFrame);

  // The axis is only relevant for percentages, so it doesn't matter what we use
  // here.
  auto axis = SVGContentUtils::X;

  return nsPresContext::CSSPixelsToAppUnits(
      aCSSValue.GetFloatValue() *
      SVGLength::GetPixelsPerCSSUnit(userSpaceMetrics, unit, axis,
                                     /* aApplyZoom = */ true));
}

/* static */
void nsMathMLFrame::GetSubDropFromChild(nsIFrame* aChild, nscoord& aSubDrop,
                                        float aFontSizeInflation) {
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForFrame(aChild, aFontSizeInflation);
  GetSubDrop(fm, aSubDrop);
}

/* static */
void nsMathMLFrame::GetSupDropFromChild(nsIFrame* aChild, nscoord& aSupDrop,
                                        float aFontSizeInflation) {
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForFrame(aChild, aFontSizeInflation);
  GetSupDrop(fm, aSupDrop);
}

/* static */
void nsMathMLFrame::ParseAndCalcNumericValue(const nsString& aString,
                                             nscoord* aLengthValue,
                                             uint32_t aFlags,
                                             float aFontSizeInflation,
                                             nsIFrame* aFrame) {
  nsCSSValue cssValue;

  if (!dom::MathMLElement::ParseNumericValue(
          aString, cssValue, aFlags, aFrame->PresContext()->Document())) {
    // Invalid attribute value. aLengthValue remains unchanged, so the default
    // length value is used.
    return;
  }

  nsCSSUnit unit = cssValue.GetUnit();

  // Since we're reusing the SVG code to calculate unit lengths,
  // which handles percentage values specially, we have to deal
  // with percentages early on.
  if (unit == eCSSUnit_Percent) {
    *aLengthValue = NSToCoordRound(*aLengthValue * cssValue.GetPercentValue());
    return;
  }

  *aLengthValue = CalcLength(cssValue, aFontSizeInflation, aFrame);
}

namespace mozilla {

class nsDisplayMathMLBar final : public nsPaintedDisplayItem {
 public:
  nsDisplayMathMLBar(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame,
                     const nsRect& aRect)
      : nsPaintedDisplayItem(aBuilder, aFrame), mRect(aRect) {
    MOZ_COUNT_CTOR(nsDisplayMathMLBar);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayMathMLBar)

  void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) override;
  NS_DISPLAY_DECL_NAME("MathMLBar", TYPE_MATHML_BAR)

 private:
  nsRect mRect;
};

void nsDisplayMathMLBar::Paint(nsDisplayListBuilder* aBuilder,
                               gfxContext* aCtx) {
  // paint the bar with the current text color
  DrawTarget* drawTarget = aCtx->GetDrawTarget();
  Rect rect = NSRectToNonEmptySnappedRect(
      mRect + ToReferenceFrame(), mFrame->PresContext()->AppUnitsPerDevPixel(),
      *drawTarget);
  ColorPattern color(ToDeviceColor(
      mFrame->GetVisitedDependentColor(&nsStyleText::mWebkitTextFillColor)));
  drawTarget->FillRect(rect, color);
}

}  // namespace mozilla

void nsMathMLFrame::DisplayBar(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame,
                               const nsRect& aRect,
                               const nsDisplayListSet& aLists,
                               uint32_t aIndex) {
  if (!aFrame->StyleVisibility()->IsVisible() || aRect.IsEmpty()) {
    return;
  }

  aLists.Content()->AppendNewToTopWithIndex<nsDisplayMathMLBar>(
      aBuilder, aFrame, aIndex, aRect);
}

void nsMathMLFrame::GetRadicalParameters(nsFontMetrics* aFontMetrics,
                                         bool aDisplayStyle,
                                         nscoord& aRadicalRuleThickness,
                                         nscoord& aRadicalExtraAscender,
                                         nscoord& aRadicalVerticalGap) {
  nscoord oneDevPixel = aFontMetrics->AppUnitsPerDevPixel();
  RefPtr<gfxFont> mathFont =
      aFontMetrics->GetThebesFontGroup()->GetFirstMathFont();

  // get the radical rulethickness
  if (mathFont) {
    aRadicalRuleThickness = mathFont->MathTable()->Constant(
        gfxMathTable::RadicalRuleThickness, oneDevPixel);
  } else {
    GetRuleThickness(aFontMetrics, aRadicalRuleThickness);
  }

  // get the leading to be left at the top of the resulting frame
  if (mathFont) {
    aRadicalExtraAscender = mathFont->MathTable()->Constant(
        gfxMathTable::RadicalExtraAscender, oneDevPixel);
  } else {
    // This seems more reliable than using aFontMetrics->GetLeading() on
    // suspicious fonts.
    nscoord em;
    GetEmHeight(aFontMetrics, em);
    aRadicalExtraAscender = nscoord(0.2f * em);
  }

  // get the clearance between rule and content
  if (mathFont) {
    aRadicalVerticalGap = mathFont->MathTable()->Constant(
        aDisplayStyle ? gfxMathTable::RadicalDisplayStyleVerticalGap
                      : gfxMathTable::RadicalVerticalGap,
        oneDevPixel);
  } else {
    // Rule 11, App. G, TeXbook
    aRadicalVerticalGap =
        aRadicalRuleThickness +
        (aDisplayStyle ? aFontMetrics->XHeight() : aRadicalRuleThickness) / 4;
  }
}
