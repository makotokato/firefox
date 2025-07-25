/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextLeafRange.h"

#include "HyperTextAccessible-inl.h"
#include "mozilla/a11y/Accessible.h"
#include "mozilla/a11y/CacheConstants.h"
#include "mozilla/a11y/DocAccessible.h"
#include "mozilla/a11y/DocAccessibleParent.h"
#include "mozilla/a11y/LocalAccessible.h"
#include "mozilla/BinarySearch.h"
#include "mozilla/Casting.h"
#include "mozilla/dom/AbstractRange.h"
#include "mozilla/dom/CharacterData.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/PresShell.h"
#include "mozilla/intl/Segmenter.h"
#include "mozilla/intl/WordBreaker.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/TextEditor.h"
#include "nsAccUtils.h"
#include "nsBlockFrame.h"
#include "nsFocusManager.h"
#include "nsFrameSelection.h"
#include "nsIAccessiblePivot.h"
#include "nsILineIterator.h"
#include "nsINode.h"
#include "nsStyleStructInlines.h"
#include "nsTArray.h"
#include "nsTextFrame.h"
#include "nsUnicharUtils.h"
#include "Pivot.h"
#include "TextAttrs.h"
#include "TextRange.h"

using mozilla::intl::WordBreaker;
using FindWordOptions = mozilla::intl::WordBreaker::FindWordOptions;

namespace mozilla::a11y {

/*** Helpers ***/

/**
 * These two functions convert between rendered and content text offsets.
 * When text DOM nodes are rendered, the rendered text often does not contain
 * all the whitespace from the source. For example, by default, the text
 * "a   b" will be rendered as "a b"; i.e. multiple spaces are compressed to
 * one. TextLeafAccessibles contain rendered text, but when we query layout, we
 * need to provide offsets into the original content text. Similarly, layout
 * returns content offsets, but we need to convert them to rendered offsets to
 * map them to TextLeafAccessibles.
 */

static int32_t RenderedToContentOffset(LocalAccessible* aAcc,
                                       uint32_t aRenderedOffset) {
  nsTextFrame* frame = do_QueryFrame(aAcc->GetFrame());
  if (!frame) {
    MOZ_ASSERT(!aAcc->HasOwnContent() || aAcc->IsHTMLBr(),
               "No text frame because this is a XUL label[value] text leaf or "
               "a BR element.");
    return static_cast<int32_t>(aRenderedOffset);
  }

  if (frame->StyleText()->WhiteSpaceIsSignificant() &&
      frame->StyleText()->NewlineIsSignificant(frame)) {
    // Spaces and new lines aren't altered, so the content and rendered offsets
    // are the same. This happens in pre-formatted text and text fields.
    return static_cast<int32_t>(aRenderedOffset);
  }

  nsIFrame::RenderedText text =
      frame->GetRenderedText(aRenderedOffset, aRenderedOffset + 1,
                             nsIFrame::TextOffsetType::OffsetsInRenderedText,
                             nsIFrame::TrailingWhitespace::DontTrim);
  return text.mOffsetWithinNodeText;
}

static uint32_t ContentToRenderedOffset(LocalAccessible* aAcc,
                                        int32_t aContentOffset) {
  nsTextFrame* frame = do_QueryFrame(aAcc->GetFrame());
  if (!frame) {
    MOZ_ASSERT(!aAcc->HasOwnContent(),
               "No text frame because this is a XUL label[value] text leaf.");
    return aContentOffset;
  }

  if (frame->StyleText()->WhiteSpaceIsSignificant() &&
      frame->StyleText()->NewlineIsSignificant(frame)) {
    // Spaces and new lines aren't altered, so the content and rendered offsets
    // are the same. This happens in pre-formatted text and text fields.
    return aContentOffset;
  }

  nsIFrame::RenderedText text =
      frame->GetRenderedText(aContentOffset, aContentOffset + 1,
                             nsIFrame::TextOffsetType::OffsetsInContentText,
                             nsIFrame::TrailingWhitespace::DontTrim);
  return text.mOffsetWithinNodeRenderedText;
}

class LeafRule : public PivotRule {
 public:
  explicit LeafRule(bool aIgnoreListItemMarker)
      : mIgnoreListItemMarker(aIgnoreListItemMarker) {}

  virtual uint16_t Match(Accessible* aAcc) override {
    if (aAcc->IsOuterDoc()) {
      // Treat an embedded doc as a single character in this document, but do
      // not descend inside it.
      return nsIAccessibleTraversalRule::FILTER_MATCH |
             nsIAccessibleTraversalRule::FILTER_IGNORE_SUBTREE;
    }

    if (mIgnoreListItemMarker && aAcc->Role() == roles::LISTITEM_MARKER) {
      // Ignore list item markers if configured to do so.
      return nsIAccessibleTraversalRule::FILTER_IGNORE;
    }

    // We deliberately include Accessibles such as empty input elements and
    // empty containers, as these can be at the start of a line.
    if (!aAcc->HasChildren()) {
      return nsIAccessibleTraversalRule::FILTER_MATCH;
    }
    return nsIAccessibleTraversalRule::FILTER_IGNORE;
  }

 private:
  bool mIgnoreListItemMarker;
};

static HyperTextAccessible* HyperTextFor(LocalAccessible* aAcc) {
  for (LocalAccessible* acc = aAcc; acc; acc = acc->LocalParent()) {
    if (HyperTextAccessible* ht = acc->AsHyperText()) {
      return ht;
    }
  }
  return nullptr;
}

static Accessible* NextLeaf(Accessible* aOrigin, bool aIsEditable = false,
                            bool aIgnoreListItemMarker = false) {
  MOZ_ASSERT(aOrigin);
  Accessible* doc = nsAccUtils::DocumentFor(aOrigin);
  Pivot pivot(doc);
  auto rule = LeafRule(aIgnoreListItemMarker);
  Accessible* leaf = pivot.Next(aOrigin, rule);
  if (aIsEditable && leaf) {
    return leaf->Parent() && (leaf->Parent()->State() & states::EDITABLE)
               ? leaf
               : nullptr;
  }
  return leaf;
}

static Accessible* PrevLeaf(Accessible* aOrigin, bool aIsEditable = false,
                            bool aIgnoreListItemMarker = false) {
  MOZ_ASSERT(aOrigin);
  Accessible* doc = nsAccUtils::DocumentFor(aOrigin);
  Pivot pivot(doc);
  auto rule = LeafRule(aIgnoreListItemMarker);
  Accessible* leaf = pivot.Prev(aOrigin, rule);
  if (aIsEditable && leaf) {
    return leaf->Parent() && (leaf->Parent()->State() & states::EDITABLE)
               ? leaf
               : nullptr;
  }
  return leaf;
}

static nsIFrame* GetFrameInBlock(const LocalAccessible* aAcc) {
  dom::HTMLInputElement* input =
      dom::HTMLInputElement::FromNodeOrNull(aAcc->GetContent());
  if (!input) {
    if (LocalAccessible* parent = aAcc->LocalParent()) {
      input = dom::HTMLInputElement::FromNodeOrNull(parent->GetContent());
    }
  }

  if (input) {
    // If this is a single line input (or a leaf of an input) we want to return
    // the top frame of the input element and not the text leaf's frame because
    // the leaf may be inside of an embedded block frame in the input's shadow
    // DOM that we aren't interested in.
    return input->GetPrimaryFrame();
  }

  return aAcc->GetFrame();
}

/**
 * Returns true if the given frames are on different lines.
 */
static bool AreFramesOnDifferentLines(nsIFrame* aFrame1, nsIFrame* aFrame2) {
  MOZ_ASSERT(aFrame1 && aFrame2);
  if (aFrame1 == aFrame2) {
    // This can happen if two Accessibles share the same frame; e.g. image maps.
    return false;
  }
  auto [block1, lineFrame1] = aFrame1->GetContainingBlockForLine(
      /* aLockScroll */ false);
  if (!block1) {
    // Error; play it safe.
    return true;
  }
  auto [block2, lineFrame2] = aFrame2->GetContainingBlockForLine(
      /* aLockScroll */ false);
  if (lineFrame1 == lineFrame2) {
    return false;
  }
  if (block1 != block2) {
    // These frames are in different blocks, so they're on different lines.
    return true;
  }
  if (nsBlockFrame* block = do_QueryFrame(block1)) {
    // If we have a block frame, it's faster for us to use
    // BlockInFlowLineIterator because it uses the line cursor.
    bool found = false;
    block->SetupLineCursorForQuery();
    nsBlockInFlowLineIterator it1(block, lineFrame1, &found);
    if (!found) {
      // Error; play it safe.
      return true;
    }
    found = false;
    nsBlockInFlowLineIterator it2(block, lineFrame2, &found);
    return !found || it1.GetLineList() != it2.GetLineList() ||
           it1.GetLine() != it2.GetLine();
  }
  AutoAssertNoDomMutations guard;
  nsILineIterator* it = block1->GetLineIterator();
  MOZ_ASSERT(it, "GetLineIterator impl in line-container blocks is infallible");
  int32_t line1 = it->FindLineContaining(lineFrame1);
  if (line1 < 0) {
    // Error; play it safe.
    return true;
  }
  int32_t line2 = it->FindLineContaining(lineFrame2, line1);
  return line1 != line2;
}

static bool IsLocalAccAtLineStart(LocalAccessible* aAcc) {
  if (aAcc->NativeRole() == roles::LISTITEM_MARKER) {
    // A bullet always starts a line.
    return true;
  }
  // Splitting of content across lines is handled by layout.
  // nsIFrame::IsLogicallyAtLineEdge queries whether a frame is the first frame
  // on its line. However, we can't use that because the first frame on a line
  // might not be included in the a11y tree; e.g. an empty span, or space
  // in the DOM after a line break which is stripped when rendered. Instead, we
  // get the line number for this Accessible's frame and the line number for the
  // previous leaf Accessible's frame and compare them.
  Accessible* prev = PrevLeaf(aAcc);
  LocalAccessible* prevLocal = prev ? prev->AsLocal() : nullptr;
  if (!prevLocal) {
    // There's nothing before us, so this is the start of the first line.
    return true;
  }
  if (prevLocal->NativeRole() == roles::LISTITEM_MARKER) {
    // If there is  a bullet immediately before us and we're inside the same
    // list item, this is not the start of a line.
    LocalAccessible* listItem = prevLocal->LocalParent();
    MOZ_ASSERT(listItem);
    LocalAccessible* doc = listItem->Document();
    MOZ_ASSERT(doc);
    for (LocalAccessible* parent = aAcc->LocalParent(); parent && parent != doc;
         parent = parent->LocalParent()) {
      if (parent == listItem) {
        return false;
      }
    }
  }

  nsIFrame* thisFrame = GetFrameInBlock(aAcc);
  if (!thisFrame) {
    return false;
  }

  nsIFrame* prevFrame = GetFrameInBlock(prevLocal);
  if (!prevFrame) {
    return false;
  }

  // The previous leaf might cross lines. We want to compare against the last
  // line.
  prevFrame = prevFrame->LastContinuation();
  // if the lines are different, that means there's nothing before us on the
  // same line, so we're at the start.
  return AreFramesOnDifferentLines(thisFrame, prevFrame);
}

/**
 * There are many kinds of word break, but we only need to treat punctuation and
 * space specially.
 */
enum WordBreakClass { eWbcSpace = 0, eWbcPunct, eWbcOther };

static WordBreakClass GetWordBreakClass(char16_t aChar) {
  // Based on IsSelectionInlineWhitespace and IsSelectionNewline in
  // layout/generic/nsTextFrame.cpp.
  const char16_t kCharNbsp = 0xA0;
  switch (aChar) {
    case ' ':
    case kCharNbsp:
    case '\t':
    case '\f':
    case '\n':
    case '\r':
      return eWbcSpace;
    default:
      break;
  }
  return mozilla::IsPunctuationForWordSelect(aChar) ? eWbcPunct : eWbcOther;
}

/**
 * Words can cross Accessibles. To work out whether we're at the start of a
 * word, we might have to check the previous leaf. This class handles querying
 * the previous WordBreakClass, crossing Accessibles if necessary.
 */
class PrevWordBreakClassWalker {
 public:
  PrevWordBreakClassWalker(Accessible* aAcc, const nsAString& aText,
                           int32_t aOffset)
      : mAcc(aAcc), mText(aText), mOffset(aOffset) {
    mClass = GetWordBreakClass(mText.CharAt(mOffset));
  }

  WordBreakClass CurClass() { return mClass; }

  Maybe<WordBreakClass> PrevClass() {
    for (;;) {
      if (!PrevChar()) {
        return Nothing();
      }
      WordBreakClass curClass = GetWordBreakClass(mText.CharAt(mOffset));
      if (curClass != mClass) {
        mClass = curClass;
        return Some(curClass);
      }
    }
    MOZ_ASSERT_UNREACHABLE();
    return Nothing();
  }

  bool IsStartOfGroup() {
    if (!PrevChar()) {
      // There are no characters before us.
      return true;
    }
    WordBreakClass curClass = GetWordBreakClass(mText.CharAt(mOffset));
    // We wanted to peek at the previous character, not really move to it.
    ++mOffset;
    return curClass != mClass;
  }

 private:
  bool PrevChar() {
    if (mOffset > 0) {
      --mOffset;
      return true;
    }
    if (!mAcc) {
      // PrevChar was called already and failed.
      return false;
    }
    mAcc = PrevLeaf(mAcc);
    if (!mAcc) {
      return false;
    }
    mText.Truncate();
    mAcc->AppendTextTo(mText);
    mOffset = static_cast<int32_t>(mText.Length()) - 1;
    return true;
  }

  Accessible* mAcc;
  nsAutoString mText;
  int32_t mOffset;
  WordBreakClass mClass;
};

/**
 * WordBreaker breaks at all space, punctuation, etc. We want to emulate
 * layout, so that's not what we want. This function determines whether this
 * is acceptable as the start of a word for our purposes.
 */
static bool IsAcceptableWordStart(Accessible* aAcc, const nsAutoString& aText,
                                  int32_t aOffset) {
  PrevWordBreakClassWalker walker(aAcc, aText, aOffset);
  if (!walker.IsStartOfGroup()) {
    // If we're not at the start of a WordBreaker group, this can't be the
    // start of a word.
    return false;
  }
  WordBreakClass curClass = walker.CurClass();
  if (curClass == eWbcSpace) {
    // Space isn't the start of a word.
    return false;
  }
  Maybe<WordBreakClass> prevClass = walker.PrevClass();
  if (curClass == eWbcPunct && (!prevClass || prevClass.value() != eWbcSpace)) {
    // Punctuation isn't the start of a word (unless it is after space).
    return false;
  }
  if (!prevClass || prevClass.value() != eWbcPunct) {
    // If there's nothing before this or the group before this isn't
    // punctuation, this is the start of a word.
    return true;
  }
  // At this point, we know the group before this is punctuation.
  if (!StaticPrefs::layout_word_select_stop_at_punctuation()) {
    // When layout.word_select.stop_at_punctuation is false (defaults to true),
    // if there is punctuation before this, this is not the start of a word.
    return false;
  }
  Maybe<WordBreakClass> prevPrevClass = walker.PrevClass();
  if (!prevPrevClass || prevPrevClass.value() == eWbcSpace) {
    // If there is punctuation before this and space (or nothing) before the
    // punctuation, this is not the start of a word.
    return false;
  }
  return true;
}

class BlockRule : public PivotRule {
 public:
  virtual uint16_t Match(Accessible* aAcc) override {
    if (RefPtr<nsAtom>(aAcc->DisplayStyle()) == nsGkAtoms::block ||
        aAcc->IsHTMLListItem() || aAcc->IsTableRow() || aAcc->IsTableCell()) {
      return nsIAccessibleTraversalRule::FILTER_MATCH;
    }
    return nsIAccessibleTraversalRule::FILTER_IGNORE;
  }
};

/**
 * Find DOM ranges which map to text attributes overlapping the requested
 * LocalAccessible and offsets. This includes ranges that begin or end outside
 * of the given LocalAccessible. Note that the offset arguments are rendered
 * offsets, but because the returned ranges are DOM ranges, those offsets are
 * content offsets. See the documentation for
 * dom::Selection::GetRangesForIntervalArray for information about the
 * aAllowAdjacent argument.
 */
static nsTArray<std::pair<nsTArray<dom::AbstractRange*>, nsStaticAtom*>>
FindDOMTextOffsetAttributes(LocalAccessible* aAcc, int32_t aRenderedStart,
                            int32_t aRenderedEnd, bool aAllowAdjacent = false) {
  nsTArray<std::pair<nsTArray<dom::AbstractRange*>, nsStaticAtom*>> result;
  if (!aAcc->IsTextLeaf() || !aAcc->HasOwnContent()) {
    return result;
  }
  nsIFrame* frame = aAcc->GetFrame();
  RefPtr<nsFrameSelection> frameSel =
      frame ? frame->GetFrameSelection() : nullptr;
  if (!frameSel) {
    return result;
  }
  nsINode* node = aAcc->GetNode();
  uint32_t contentStart = RenderedToContentOffset(aAcc, aRenderedStart);
  uint32_t contentEnd =
      aRenderedEnd == nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT
          ? dom::CharacterData::FromNode(node)->TextLength()
          : RenderedToContentOffset(aAcc, aRenderedEnd);
  const std::pair<mozilla::SelectionType, nsStaticAtom*>
      kSelectionTypesToAttributes[] = {
          {SelectionType::eSpellCheck, nsGkAtoms::spelling},
          {SelectionType::eTargetText, nsGkAtoms::mark},
      };
  size_t highlightCount = frameSel->HighlightSelectionCount();
  result.SetCapacity(std::size(kSelectionTypesToAttributes) + highlightCount);

  auto appendRanges = [&](dom::Selection* aDomSel, nsStaticAtom* aAttr) {
    nsTArray<dom::AbstractRange*> domRanges;
    aDomSel->GetAbstractRangesForIntervalArray(
        node, contentStart, node, contentEnd, aAllowAdjacent, &domRanges);
    if (!domRanges.IsEmpty()) {
      result.AppendElement(std::make_pair(std::move(domRanges), aAttr));
    }
  };

  for (auto [selType, attr] : kSelectionTypesToAttributes) {
    dom::Selection* domSel = frameSel->GetSelection(selType);
    if (!domSel) {
      continue;
    }
    appendRanges(domSel, attr);
  }

  for (size_t h = 0; h < highlightCount; ++h) {
    RefPtr<dom::Selection> domSel = frameSel->HighlightSelection(h);
    MOZ_ASSERT(domSel);
    nsStaticAtom* attr = nullptr;
    MOZ_ASSERT(domSel->HighlightSelectionData().mHighlight);
    switch (domSel->HighlightSelectionData().mHighlight->Type()) {
      case dom::HighlightType::Highlight:
        attr = nsGkAtoms::mark;
        break;
      case dom::HighlightType::Spelling_error:
        attr = nsGkAtoms::spelling;
        break;
      case dom::HighlightType::Grammar_error:
        attr = nsGkAtoms::grammar;
        break;
    }
    MOZ_ASSERT(attr);
    appendRanges(domSel, attr);
  }

  return result;
}

/**
 * Given two DOM nodes get DOM Selection object that is common
 * to both of them.
 */
static dom::Selection* GetDOMSelection(const nsIContent* aStartContent,
                                       const nsIContent* aEndContent) {
  nsIFrame* startFrame = aStartContent->GetPrimaryFrame();
  const nsFrameSelection* startFrameSel =
      startFrame ? startFrame->GetConstFrameSelection() : nullptr;
  nsIFrame* endFrame = aEndContent->GetPrimaryFrame();
  const nsFrameSelection* endFrameSel =
      endFrame ? endFrame->GetConstFrameSelection() : nullptr;

  if (startFrameSel != endFrameSel) {
    // Start and end point don't share the same selection state.
    // This could happen when both points aren't in the same editable.
    return nullptr;
  }

  return startFrameSel ? &startFrameSel->NormalSelection() : nullptr;
}

std::pair<nsIContent*, uint32_t> TextLeafPoint::ToDOMPoint(
    bool aIncludeGenerated) const {
  if (!(*this) || !mAcc->IsLocal()) {
    MOZ_ASSERT_UNREACHABLE("Invalid point");
    return {nullptr, 0};
  }

  nsIContent* content = mAcc->AsLocal()->GetContent();
  nsIFrame* frame = content ? content->GetPrimaryFrame() : nullptr;
  MOZ_ASSERT(frame);

  if (!aIncludeGenerated && frame && frame->IsGeneratedContentFrame()) {
    // List markers accessibles represent the generated content element,
    // before/after text accessibles represent the child text nodes.
    auto generatedElement = content->IsGeneratedContentContainerForMarker()
                                ? content
                                : content->GetParentElement();
    auto parent = generatedElement ? generatedElement->GetParent() : nullptr;
    MOZ_ASSERT(parent);
    if (parent) {
      if (generatedElement->IsGeneratedContentContainerForAfter()) {
        // Use the end offset of the parent element for trailing generated
        // content.
        return {parent, parent->GetChildCount()};
      }

      if (generatedElement->IsGeneratedContentContainerForBefore() ||
          generatedElement->IsGeneratedContentContainerForMarker()) {
        // Use the start offset of the parent element for leading generated
        // content.
        return {parent, 0};
      }

      MOZ_ASSERT_UNREACHABLE("Unknown generated content type!");
    }
  }

  if (mAcc->IsTextLeaf()) {
    // For text nodes, DOM uses a character offset within the node.
    return {content, RenderedToContentOffset(mAcc->AsLocal(), mOffset)};
  }

  if (!mAcc->IsHyperText()) {
    // For non-text nodes (e.g. images), DOM points use the child index within
    // the parent. mOffset could be 0 (for the start of the node) or 1 (for the
    // end of the node). mOffset could be 1 if this is the last Accessible in a
    // container and the point is at the end of the container.
    MOZ_ASSERT(mOffset == 0 || mOffset == 1);
    nsIContent* parent = content->GetParent();
    MOZ_ASSERT(parent);
    // ComputeIndexOf() could return Nothing if this is an anonymous child.
    if (auto childIndex = parent->ComputeIndexOf(content)) {
      return {parent, mOffset == 0 ? *childIndex : *childIndex + 1};
    }
  }

  // This could be an empty editable container, whitespace or an empty doc. In
  // any case, the offset inside should be 0.
  MOZ_ASSERT(mOffset == 0);

  if (RefPtr<TextControlElement> textControlElement =
          TextControlElement::FromNodeOrNull(content)) {
    // This is an empty input, use the shadow root's element.
    if (RefPtr<TextEditor> textEditor = textControlElement->GetTextEditor()) {
      if (textEditor->IsEmpty()) {
        MOZ_ASSERT(mOffset == 0);
        return {textEditor->GetRoot(), 0};
      }
    }
  }

  return {content, 0};
}

static bool IsLineBreakContinuation(nsTextFrame* aContinuation) {
  // A fluid continuation always means a new line.
  if (aContinuation->HasAnyStateBits(NS_FRAME_IS_FLUID_CONTINUATION)) {
    return true;
  }
  // If both this continuation and the previous continuation are bidi
  // continuations, this continuation might be both a bidi split and on a new
  // line.
  if (!aContinuation->HasAnyStateBits(NS_FRAME_IS_BIDI)) {
    return true;
  }
  nsTextFrame* prev = aContinuation->GetPrevContinuation();
  if (!prev) {
    // aContinuation is the primary frame. We can't be sure if this starts a new
    // line, as there might be other nodes before it. That is handled by
    // IsLocalAccAtLineStart.
    return false;
  }
  if (!prev->HasAnyStateBits(NS_FRAME_IS_BIDI)) {
    return true;
  }
  return AreFramesOnDifferentLines(aContinuation, prev);
}

static bool IsCaretValid(TextLeafPoint aPoint) {
  Accessible* acc = aPoint.mAcc;
  if (!acc->IsHyperText()) {
    acc = acc->Parent();
  }
  if (!(acc->State() & states::EDITABLE)) {
    return true;
  }
  // The caret is within editable content.
  Accessible* focus = FocusMgr() ? FocusMgr()->FocusedAccessible() : nullptr;
  if (!focus) {
    return false;
  }
  // If the focus isn't an editor, the caret can't be inside an editor. This
  // can happen, for example, when a text input is the last element in a
  // container and a user clicks in the empty area at the end of the container.
  // In this case, the caret is actually at the end of the container outside the
  // input. This can also happen if there is an empty area in a container before
  // an input and a user clicks there. TextLeafPoint can't represent either of
  // these cases and it's generally not useful. We must not normalize this to
  // the nearest leaf because this would put the caret inside an editor which
  // isn't focused. Instead, we pretend there is no caret. See bug 1950748 for
  // more details.
  return focus->State() & states::EDITABLE;
}

/*** TextLeafPoint ***/

TextLeafPoint::TextLeafPoint(Accessible* aAcc, int32_t aOffset) {
  MOZ_ASSERT(aOffset >= 0 ||
             aOffset == nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT);
  if (!aAcc) {
    // Construct an invalid point.
    mAcc = nullptr;
    mOffset = 0;
    return;
  }

  // Even though an OuterDoc contains a document, we treat it as a leaf because
  // we don't want to move into another document.
  if (!aAcc->IsOuterDoc() && aAcc->HasChildren()) {
    // Find a leaf. This might not necessarily be a TextLeafAccessible; it
    // could be an empty container.
    auto GetChild = [&aOffset](Accessible* acc) -> Accessible* {
      if (acc->IsOuterDoc()) {
        return nullptr;
      }
      return aOffset != nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT
                 ? acc->FirstChild()
                 : acc->LastChild();
    };

    for (Accessible* acc = GetChild(aAcc); acc; acc = GetChild(acc)) {
      mAcc = acc;
    }
    mOffset = aOffset != nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT
                  ? 0
                  : nsAccUtils::TextLength(mAcc);
    return;
  }
  mAcc = aAcc;
  mOffset = aOffset != nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT
                ? aOffset
                : nsAccUtils::TextLength(mAcc);
}

bool TextLeafPoint::operator<(const TextLeafPoint& aPoint) const {
  if (mAcc == aPoint.mAcc) {
    return mOffset < aPoint.mOffset;
  }
  return mAcc->IsBefore(aPoint.mAcc);
}

bool TextLeafPoint::operator<=(const TextLeafPoint& aPoint) const {
  return *this == aPoint || *this < aPoint;
}

bool TextLeafPoint::IsDocEdge(nsDirection aDirection) const {
  if (aDirection == eDirPrevious) {
    return mOffset == 0 && !PrevLeaf(mAcc);
  }

  return mOffset == static_cast<int32_t>(nsAccUtils::TextLength(mAcc)) &&
         !NextLeaf(mAcc);
}

bool TextLeafPoint::IsLeafAfterListItemMarker() const {
  Accessible* prev = PrevLeaf(mAcc);
  return prev && prev->Role() == roles::LISTITEM_MARKER &&
         prev->Parent()->IsAncestorOf(mAcc);
}

bool TextLeafPoint::IsEmptyLastLine() const {
  if (mAcc->IsHTMLBr() && mOffset == 1) {
    return true;
  }
  if (!mAcc->IsTextLeaf()) {
    return false;
  }
  if (mOffset < static_cast<int32_t>(nsAccUtils::TextLength(mAcc))) {
    return false;
  }
  nsAutoString text;
  mAcc->AppendTextTo(text, mOffset - 1, 1);
  return text.CharAt(0) == '\n';
}

char16_t TextLeafPoint::GetChar() const {
  nsAutoString text;
  mAcc->AppendTextTo(text, mOffset, 1);
  return text.CharAt(0);
}

TextLeafPoint TextLeafPoint::FindPrevLineStartSameLocalAcc(
    bool aIncludeOrigin) const {
  LocalAccessible* acc = mAcc->AsLocal();
  MOZ_ASSERT(acc);
  if (mOffset == 0) {
    if (aIncludeOrigin && IsLocalAccAtLineStart(acc)) {
      return *this;
    }
    return TextLeafPoint();
  }
  nsIFrame* frame = acc->GetFrame();
  if (!frame) {
    // This can happen if this is an empty element with display: contents. In
    // that case, this Accessible contains no lines.
    return TextLeafPoint();
  }
  if (!frame->IsTextFrame()) {
    if (IsLocalAccAtLineStart(acc)) {
      return TextLeafPoint(acc, 0);
    }
    return TextLeafPoint();
  }
  // Each line of a text node is rendered as a continuation frame. Get the
  // continuation containing the origin.
  int32_t origOffset = mOffset;
  origOffset = RenderedToContentOffset(acc, origOffset);
  nsTextFrame* continuation = nullptr;
  int32_t unusedOffsetInContinuation = 0;
  frame->GetChildFrameContainingOffset(
      origOffset, true, &unusedOffsetInContinuation, (nsIFrame**)&continuation);
  MOZ_ASSERT(continuation);
  int32_t lineStart = continuation->GetContentOffset();
  if (lineStart > 0 && (
                           // A line starts at the origin, but the caller
                           // doesn't want this included.
                           (!aIncludeOrigin && lineStart == origOffset) ||
                           !IsLineBreakContinuation(continuation))) {
    // Go back one more, skipping continuations that aren't line breaks or the
    // primary frame.
    for (nsTextFrame* prev = continuation->GetPrevContinuation(); prev;
         prev = prev->GetPrevContinuation()) {
      continuation = prev;
      if (IsLineBreakContinuation(continuation)) {
        break;
      }
    }
    MOZ_ASSERT(continuation);
    lineStart = continuation->GetContentOffset();
  }
  MOZ_ASSERT(lineStart >= 0);
  MOZ_ASSERT(lineStart == 0 || IsLineBreakContinuation(continuation));
  if (lineStart == 0 && !IsLocalAccAtLineStart(acc)) {
    // This is the first line of this text node, but there is something else
    // on the same line before this text node, so don't return this as a line
    // start.
    return TextLeafPoint();
  }
  lineStart = static_cast<int32_t>(ContentToRenderedOffset(acc, lineStart));
  return TextLeafPoint(acc, lineStart);
}

TextLeafPoint TextLeafPoint::FindNextLineStartSameLocalAcc(
    bool aIncludeOrigin) const {
  LocalAccessible* acc = mAcc->AsLocal();
  MOZ_ASSERT(acc);
  if (aIncludeOrigin && mOffset == 0 && IsLocalAccAtLineStart(acc)) {
    return *this;
  }
  nsIFrame* frame = acc->GetFrame();
  if (!frame) {
    // This can happen if this is an empty element with display: contents. In
    // that case, this Accessible contains no lines.
    return TextLeafPoint();
  }
  if (!frame->IsTextFrame()) {
    // There can't be multiple lines in a non-text leaf.
    return TextLeafPoint();
  }
  // Each line of a text node is rendered as a continuation frame. Get the
  // continuation containing the origin.
  int32_t origOffset = mOffset;
  origOffset = RenderedToContentOffset(acc, origOffset);
  nsTextFrame* continuation = nullptr;
  int32_t unusedOffsetInContinuation = 0;
  frame->GetChildFrameContainingOffset(
      origOffset, true, &unusedOffsetInContinuation, (nsIFrame**)&continuation);
  MOZ_ASSERT(continuation);
  if (
      // A line starts at the origin and the caller wants this included.
      aIncludeOrigin && continuation->GetContentOffset() == origOffset &&
      IsLineBreakContinuation(continuation) &&
      // If this is the first line of this text node (offset 0), don't treat it
      // as a line start if there's something else on the line before this text
      // node.
      !(origOffset == 0 && !IsLocalAccAtLineStart(acc))) {
    return *this;
  }
  // Get the next continuation, skipping continuations that aren't line breaks.
  while ((continuation = continuation->GetNextContinuation())) {
    if (IsLineBreakContinuation(continuation)) {
      break;
    }
  }
  if (!continuation) {
    return TextLeafPoint();
  }
  int32_t lineStart = continuation->GetContentOffset();
  lineStart = static_cast<int32_t>(ContentToRenderedOffset(acc, lineStart));
  return TextLeafPoint(acc, lineStart);
}

TextLeafPoint TextLeafPoint::FindLineStartSameRemoteAcc(
    nsDirection aDirection, bool aIncludeOrigin) const {
  RemoteAccessible* acc = mAcc->AsRemote();
  MOZ_ASSERT(acc);
  auto lines = acc->GetCachedTextLines();
  if (!lines) {
    return TextLeafPoint();
  }
  size_t index;
  // If BinarySearch returns true, mOffset is in the array and index points at
  // it. If BinarySearch returns false, mOffset is not in the array and index
  // points at the next line start after mOffset.
  if (BinarySearch(*lines, 0, lines->Length(), mOffset, &index)) {
    if (aIncludeOrigin) {
      return *this;
    }
    if (aDirection == eDirNext) {
      // We don't want to include the origin. Get the next line start.
      ++index;
    }
  }
  MOZ_ASSERT(index <= lines->Length());
  if ((aDirection == eDirNext && index == lines->Length()) ||
      (aDirection == eDirPrevious && index == 0)) {
    return TextLeafPoint();
  }
  // index points at the line start after mOffset.
  if (aDirection == eDirPrevious) {
    --index;
  }
  return TextLeafPoint(mAcc, lines->ElementAt(index));
}

TextLeafPoint TextLeafPoint::FindLineStartSameAcc(
    nsDirection aDirection, bool aIncludeOrigin,
    bool aIgnoreListItemMarker) const {
  TextLeafPoint boundary;
  if (aIgnoreListItemMarker && aIncludeOrigin && mOffset == 0 &&
      IsLeafAfterListItemMarker()) {
    // If:
    // (1) we are ignoring list markers
    // (2) we should include origin
    // (3) we are at the start of a leaf that follows a list item marker
    // ...then return this point.
    return *this;
  }

  if (mAcc->IsLocal()) {
    boundary = aDirection == eDirNext
                   ? FindNextLineStartSameLocalAcc(aIncludeOrigin)
                   : FindPrevLineStartSameLocalAcc(aIncludeOrigin);
  } else {
    boundary = FindLineStartSameRemoteAcc(aDirection, aIncludeOrigin);
  }

  if (aIgnoreListItemMarker && aDirection == eDirPrevious && !boundary &&
      mOffset != 0 && IsLeafAfterListItemMarker()) {
    // If:
    // (1) we are ignoring list markers
    // (2) we are searching backwards in accessible
    // (3) we did not find a line start before this point
    // (4) we are in a leaf that follows a list item marker
    // ...then return the first point in this accessible.
    boundary = TextLeafPoint(mAcc, 0);
  }

  return boundary;
}

TextLeafPoint TextLeafPoint::FindPrevWordStartSameAcc(
    bool aIncludeOrigin) const {
  if (mOffset == 0 && !aIncludeOrigin) {
    // We can't go back any further and the caller doesn't want the origin
    // included, so there's nothing more to do.
    return TextLeafPoint();
  }
  nsAutoString text;
  mAcc->AppendTextTo(text);
  TextLeafPoint lineStart = *this;
  if (!aIncludeOrigin || (lineStart.mOffset == 1 && text.Length() == 1 &&
                          text.CharAt(0) == '\n')) {
    // We're not interested in a line that starts here, either because
    // aIncludeOrigin is false or because we're at the end of a line break
    // node.
    --lineStart.mOffset;
  }
  // A word never starts with a line feed character. If there are multiple
  // consecutive line feed characters and we're after the first of them, the
  // previous line start will be a line feed character. Skip this and any prior
  // consecutive line feed first.
  for (; lineStart.mOffset >= 0 && text.CharAt(lineStart.mOffset) == '\n';
       --lineStart.mOffset) {
  }
  if (lineStart.mOffset < 0) {
    // There's no line start for our purposes.
    lineStart = TextLeafPoint();
  } else {
    lineStart =
        lineStart.FindLineStartSameAcc(eDirPrevious, /* aIncludeOrigin */ true);
  }
  // Keep walking backward until we find an acceptable word start.
  intl::WordRange word;
  if (mOffset == 0) {
    word.mBegin = 0;
  } else if (mOffset == static_cast<int32_t>(text.Length())) {
    word = WordBreaker::FindWord(
        text, mOffset - 1,
        StaticPrefs::layout_word_select_stop_at_punctuation()
            ? FindWordOptions::StopAtPunctuation
            : FindWordOptions::None);
  } else {
    word = WordBreaker::FindWord(
        text, mOffset,
        StaticPrefs::layout_word_select_stop_at_punctuation()
            ? FindWordOptions::StopAtPunctuation
            : FindWordOptions::None);
  }
  for (;; word = WordBreaker::FindWord(
              text, word.mBegin - 1,
              StaticPrefs::layout_word_select_stop_at_punctuation()
                  ? FindWordOptions::StopAtPunctuation
                  : FindWordOptions::None)) {
    if (!aIncludeOrigin && static_cast<int32_t>(word.mBegin) == mOffset) {
      // A word possibly starts at the origin, but the caller doesn't want this
      // included.
      MOZ_ASSERT(word.mBegin != 0);
      continue;
    }
    if (lineStart && static_cast<int32_t>(word.mBegin) < lineStart.mOffset) {
      // A line start always starts a new word.
      return lineStart;
    }
    if (IsAcceptableWordStart(mAcc, text, static_cast<int32_t>(word.mBegin))) {
      break;
    }
    if (word.mBegin == 0) {
      // We can't go back any further.
      if (lineStart) {
        // A line start always starts a new word.
        return lineStart;
      }
      return TextLeafPoint();
    }
  }
  return TextLeafPoint(mAcc, static_cast<int32_t>(word.mBegin));
}

TextLeafPoint TextLeafPoint::FindNextWordStartSameAcc(
    bool aIncludeOrigin) const {
  nsAutoString text;
  mAcc->AppendTextTo(text);
  int32_t wordStart = mOffset;
  if (aIncludeOrigin) {
    if (wordStart == 0) {
      if (IsAcceptableWordStart(mAcc, text, 0)) {
        return *this;
      }
    } else {
      // The origin might start a word, so search from just before it.
      --wordStart;
    }
  }
  TextLeafPoint lineStart = FindLineStartSameAcc(eDirNext, aIncludeOrigin);
  if (lineStart) {
    // A word never starts with a line feed character. If there are multiple
    // consecutive line feed characters, lineStart will point at the second of
    // them. Skip this and any subsequent consecutive line feed.
    for (; lineStart.mOffset < static_cast<int32_t>(text.Length()) &&
           text.CharAt(lineStart.mOffset) == '\n';
         ++lineStart.mOffset) {
    }
    if (lineStart.mOffset == static_cast<int32_t>(text.Length())) {
      // There's no line start for our purposes.
      lineStart = TextLeafPoint();
    }
  }
  // Keep walking forward until we find an acceptable word start.
  intl::WordBreakIteratorUtf16 wordBreakIter(text);
  int32_t previousPos = wordStart;
  Maybe<uint32_t> nextBreak = wordBreakIter.Seek(wordStart);
  for (;;) {
    if (!nextBreak || *nextBreak == text.Length()) {
      if (lineStart) {
        // A line start always starts a new word.
        return lineStart;
      }
      if (StaticPrefs::layout_word_select_stop_at_punctuation()) {
        // If layout.word_select.stop_at_punctuation is true, we have to look
        // for punctuation class since it may not break state in UAX#29.
        for (int32_t i = previousPos + 1;
             i < static_cast<int32_t>(text.Length()); i++) {
          if (IsAcceptableWordStart(mAcc, text, i)) {
            return TextLeafPoint(mAcc, i);
          }
        }
      }
      return TextLeafPoint();
    }
    wordStart = AssertedCast<int32_t>(*nextBreak);
    if (lineStart && wordStart > lineStart.mOffset) {
      // A line start always starts a new word.
      return lineStart;
    }
    if (IsAcceptableWordStart(mAcc, text, wordStart)) {
      break;
    }

    if (StaticPrefs::layout_word_select_stop_at_punctuation()) {
      // If layout.word_select.stop_at_punctuation is true, we have to look
      // for punctuation class since it may not break state in UAX#29.
      for (int32_t i = previousPos + 1; i < wordStart; i++) {
        if (IsAcceptableWordStart(mAcc, text, i)) {
          return TextLeafPoint(mAcc, i);
        }
      }
    }
    previousPos = wordStart;
    nextBreak = wordBreakIter.Next();
  }
  return TextLeafPoint(mAcc, wordStart);
}

/* static */
TextLeafPoint TextLeafPoint::GetCaret(Accessible* aAcc) {
  if (LocalAccessible* localAcc = aAcc->AsLocal()) {
    // Use HyperTextAccessible::CaretOffset. Eventually, we'll want to move
    // that code into TextLeafPoint, but existing code depends on it living in
    // HyperTextAccessible (including caret events).
    HyperTextAccessible* ht = HyperTextFor(localAcc);
    if (!ht) {
      return TextLeafPoint();
    }
    int32_t htOffset = ht->CaretOffset();
    if (htOffset == -1) {
      return TextLeafPoint();
    }
    TextLeafPoint point = ht->ToTextLeafPoint(htOffset);
    if (!point) {
      // Bug 1905021: This happens in the wild, but we don't understand why.
      // ToTextLeafPoint should only fail if the HyperText offset is invalid,
      // but CaretOffset shouldn't return an invalid offset.
      MOZ_ASSERT_UNREACHABLE(
          "Got HyperText CaretOffset but ToTextLeafPoint failed");
      return point;
    }
    if (!IsCaretValid(point)) {
      return TextLeafPoint();
    }
    nsIFrame* frame = ht->GetFrame();
    RefPtr<nsFrameSelection> sel = frame ? frame->GetFrameSelection() : nullptr;
    if (sel && sel->GetHint() == CaretAssociationHint::Before) {
      // CaretAssociationHint::Before can mean that the caret is at the end of
      // a line. However, this can also occur in a few other situations:
      // 1. The caret is before the start of a node in the middle of a line.
      // This happens when moving the cursor forward to a new node.
      // 2. The user clicks the mouse on a character other than the first in a
      // node.
      // 3. The caret is somewhere other than the start of a line and the user
      // presses down or up arrow to move by line.
      if (point.mOffset <
          static_cast<int32_t>(nsAccUtils::TextLength(point.mAcc))) {
        // The caret is at the end of a line if the point is at the start of a
        // line but not at the start of a paragraph.
        point.mIsEndOfLineInsertionPoint =
            point.FindPrevLineStartSameLocalAcc(/* aIncludeOrigin */ true) ==
                point &&
            !point.IsParagraphStart();
      } else {
        // This is the end of a node. CaretAssociationHint::Before is only used
        // at the end of a node if the caret is at the end of a line.
        point.mIsEndOfLineInsertionPoint = true;
      }
    }
    return point;
  }

  // Ideally, we'd cache the caret as a leaf, but our events are based on
  // HyperText for now.
  DocAccessibleParent* remoteDoc = aAcc->AsRemote()->Document();
  auto [ht, htOffset] = remoteDoc->GetCaret();
  if (!ht) {
    return TextLeafPoint();
  }
  TextLeafPoint point = ht->ToTextLeafPoint(htOffset);
  if (!point) {
    // The caret offset should usually be in sync with the tree. However, tree
    // and selection updates happen using separate IPDL calls, so it's possible
    // for a client caret query to arrive between them. Thus, we can end up
    // with an invalid caret here.
    return point;
  }
  if (!IsCaretValid(point)) {
    return TextLeafPoint();
  }
  point.mIsEndOfLineInsertionPoint = remoteDoc->IsCaretAtEndOfLine();
  return point;
}

TextLeafPoint TextLeafPoint::AdjustEndOfLine() const {
  MOZ_ASSERT(mIsEndOfLineInsertionPoint);
  // Use the last character on the line so that we search for word and line
  // boundaries on the current line, not the next line.
  return TextLeafPoint(mAcc, mOffset)
      .FindBoundary(nsIAccessibleText::BOUNDARY_CHAR, eDirPrevious);
}

TextLeafPoint TextLeafPoint::FindBoundary(AccessibleTextBoundary aBoundaryType,
                                          nsDirection aDirection,
                                          BoundaryFlags aFlags) const {
  if (mIsEndOfLineInsertionPoint) {
    // In this block, we deliberately don't propagate mIsEndOfLineInsertionPoint
    // to derived points because otherwise, a call to FindBoundary on the
    // returned point would also return the same point.
    if (aBoundaryType == nsIAccessibleText::BOUNDARY_CHAR ||
        aBoundaryType == nsIAccessibleText::BOUNDARY_CLUSTER) {
      if (aDirection == eDirNext || (aDirection == eDirPrevious &&
                                     aFlags & BoundaryFlags::eIncludeOrigin)) {
        // The caller wants the current or next character/cluster. Return no
        // character, since otherwise, this would move past the first character
        // on the next line.
        return TextLeafPoint(mAcc, mOffset);
      }
      // The caller wants the previous character/cluster. Return that as normal.
      return TextLeafPoint(mAcc, mOffset)
          .FindBoundary(aBoundaryType, aDirection, aFlags);
    }
    // For any other boundary, we need to start on this line, not the next, even
    // though mOffset refers to the next.
    return AdjustEndOfLine().FindBoundary(aBoundaryType, aDirection, aFlags);
  }

  bool inEditableAndStopInIt = (aFlags & BoundaryFlags::eStopInEditable) &&
                               mAcc->Parent() &&
                               (mAcc->Parent()->State() & states::EDITABLE);
  if (aBoundaryType == nsIAccessibleText::BOUNDARY_LINE_END) {
    return FindLineEnd(aDirection,
                       inEditableAndStopInIt
                           ? aFlags
                           : (aFlags & ~BoundaryFlags::eStopInEditable));
  }
  if (aBoundaryType == nsIAccessibleText::BOUNDARY_WORD_END) {
    return FindWordEnd(aDirection,
                       inEditableAndStopInIt
                           ? aFlags
                           : (aFlags & ~BoundaryFlags::eStopInEditable));
  }
  if ((aBoundaryType == nsIAccessibleText::BOUNDARY_LINE_START ||
       aBoundaryType == nsIAccessibleText::BOUNDARY_PARAGRAPH) &&
      (aFlags & BoundaryFlags::eIncludeOrigin) && aDirection == eDirPrevious &&
      IsEmptyLastLine()) {
    // If we're at an empty line at the end of an Accessible,  we don't want to
    // walk into the previous line. For example, this can happen if the caret
    // is positioned on an empty line at the end of a textarea.
    return *this;
  }
  bool includeOrigin = !!(aFlags & BoundaryFlags::eIncludeOrigin);
  bool ignoreListItemMarker = !!(aFlags & BoundaryFlags::eIgnoreListItemMarker);
  Accessible* lastAcc = nullptr;
  for (TextLeafPoint searchFrom = *this; searchFrom;
       searchFrom = searchFrom.NeighborLeafPoint(
           aDirection, inEditableAndStopInIt, ignoreListItemMarker)) {
    lastAcc = searchFrom.mAcc;
    if (ignoreListItemMarker && searchFrom == *this &&
        searchFrom.mAcc->Role() == roles::LISTITEM_MARKER) {
      continue;
    }
    TextLeafPoint boundary;
    // Search for the boundary within the current Accessible.
    switch (aBoundaryType) {
      case nsIAccessibleText::BOUNDARY_CHAR:
        if (includeOrigin) {
          boundary = searchFrom;
        } else if (aDirection == eDirPrevious && searchFrom.mOffset > 0) {
          boundary.mAcc = searchFrom.mAcc;
          boundary.mOffset = searchFrom.mOffset - 1;
        } else if (aDirection == eDirNext &&
                   searchFrom.mOffset + 1 <
                       static_cast<int32_t>(
                           nsAccUtils::TextLength(searchFrom.mAcc))) {
          boundary.mAcc = searchFrom.mAcc;
          boundary.mOffset = searchFrom.mOffset + 1;
        }
        break;
      case nsIAccessibleText::BOUNDARY_WORD_START:
        if (aDirection == eDirPrevious) {
          boundary = searchFrom.FindPrevWordStartSameAcc(includeOrigin);
        } else {
          boundary = searchFrom.FindNextWordStartSameAcc(includeOrigin);
        }
        break;
      case nsIAccessibleText::BOUNDARY_LINE_START:
        boundary = searchFrom.FindLineStartSameAcc(aDirection, includeOrigin,
                                                   ignoreListItemMarker);
        break;
      case nsIAccessibleText::BOUNDARY_PARAGRAPH:
        boundary = searchFrom.FindParagraphSameAcc(aDirection, includeOrigin,
                                                   ignoreListItemMarker);
        break;
      case nsIAccessibleText::BOUNDARY_CLUSTER:
        boundary = searchFrom.FindClusterSameAcc(aDirection, includeOrigin);
        break;
      default:
        MOZ_ASSERT_UNREACHABLE();
        break;
    }
    if (boundary) {
      return boundary;
    }

    // The start/end of the Accessible might be a boundary. If so, we must stop
    // on it.
    includeOrigin = true;
  }

  MOZ_ASSERT(lastAcc);
  // No further leaf was found. Use the start/end of the first/last leaf.
  return TextLeafPoint(
      lastAcc, aDirection == eDirPrevious
                   ? 0
                   : static_cast<int32_t>(nsAccUtils::TextLength(lastAcc)));
}

TextLeafPoint TextLeafPoint::FindLineEnd(nsDirection aDirection,
                                         BoundaryFlags aFlags) const {
  if (aDirection == eDirPrevious && IsEmptyLastLine()) {
    // If we're at an empty line at the end of an Accessible,  we don't want to
    // walk into the previous line. For example, this can happen if the caret
    // is positioned on an empty line at the end of a textarea.
    // Because we want the line end, we must walk back to the line feed
    // character.
    return FindBoundary(nsIAccessibleText::BOUNDARY_CHAR, eDirPrevious,
                        aFlags & ~BoundaryFlags::eIncludeOrigin);
  }
  if ((aFlags & BoundaryFlags::eIncludeOrigin) && IsLineFeedChar()) {
    return *this;
  }
  if (aDirection == eDirPrevious && !(aFlags & BoundaryFlags::eIncludeOrigin)) {
    // If there is a line feed immediately before us, return that.
    TextLeafPoint prevChar =
        FindBoundary(nsIAccessibleText::BOUNDARY_CHAR, eDirPrevious,
                     aFlags & ~BoundaryFlags::eIncludeOrigin);
    if (prevChar.IsLineFeedChar()) {
      return prevChar;
    }
  }
  TextLeafPoint searchFrom = *this;
  if (aDirection == eDirNext && IsLineFeedChar()) {
    // If we search for the next line start from a line feed, we'll get the
    // character immediately following the line feed. We actually want the
    // next line start after that. Skip the line feed.
    searchFrom = FindBoundary(nsIAccessibleText::BOUNDARY_CHAR, eDirNext,
                              aFlags & ~BoundaryFlags::eIncludeOrigin);
  }
  TextLeafPoint lineStart = searchFrom.FindBoundary(
      nsIAccessibleText::BOUNDARY_LINE_START, aDirection, aFlags);
  if (aDirection == eDirNext && IsEmptyLastLine()) {
    // There is a line feed immediately before us, but that's actually the end
    // of the previous line, not the end of our empty line. Don't walk back.
    return lineStart;
  }
  // If there is a line feed before this line start (at the end of the previous
  // line), we must return that.
  TextLeafPoint prevChar =
      lineStart.FindBoundary(nsIAccessibleText::BOUNDARY_CHAR, eDirPrevious,
                             aFlags & ~BoundaryFlags::eIncludeOrigin);
  if (prevChar && prevChar.IsLineFeedChar()) {
    return prevChar;
  }
  return lineStart;
}

bool TextLeafPoint::IsSpace() const {
  return GetWordBreakClass(GetChar()) == eWbcSpace;
}

TextLeafPoint TextLeafPoint::FindWordEnd(nsDirection aDirection,
                                         BoundaryFlags aFlags) const {
  char16_t origChar = GetChar();
  const bool origIsSpace = GetWordBreakClass(origChar) == eWbcSpace;
  bool prevIsSpace = false;
  if (aDirection == eDirPrevious ||
      ((aFlags & BoundaryFlags::eIncludeOrigin) && origIsSpace) || !origChar) {
    TextLeafPoint prev =
        FindBoundary(nsIAccessibleText::BOUNDARY_CHAR, eDirPrevious,
                     aFlags & ~BoundaryFlags::eIncludeOrigin);
    if (aDirection == eDirPrevious && prev == *this) {
      return *this;  // Can't go any further.
    }
    prevIsSpace = prev.IsSpace();
    if ((aFlags & BoundaryFlags::eIncludeOrigin) &&
        (origIsSpace || IsDocEdge(eDirNext)) && !prevIsSpace) {
      // The origin is space or end of document, but the previous
      // character is not. This means we're at the end of a word.
      return *this;
    }
  }
  TextLeafPoint boundary = *this;
  if (aDirection == eDirPrevious && !prevIsSpace) {
    // If there isn't space immediately before us, first find the start of the
    // previous word.
    boundary = FindBoundary(nsIAccessibleText::BOUNDARY_WORD_START,
                            eDirPrevious, aFlags);
  } else if (aDirection == eDirNext &&
             (origIsSpace || (!origChar && prevIsSpace))) {
    // We're within the space at the end of the word. Skip over the space. We
    // can do that by searching for the next word start.
    boundary = FindBoundary(nsIAccessibleText::BOUNDARY_WORD_START, eDirNext,
                            aFlags & ~BoundaryFlags::eIncludeOrigin);
    if (boundary.IsSpace()) {
      // The next word starts with a space. This can happen if there is a space
      // after or at the start of a block element.
      return boundary;
    }
  }
  if (aDirection == eDirNext) {
    BoundaryFlags flags = aFlags;
    if (IsDocEdge(eDirPrevious)) {
      // If this is the start of the doc don't be inclusive in the word-start
      // search because there is no preceding block where this could be a
      // word-end for.
      flags &= ~BoundaryFlags::eIncludeOrigin;
    }
    boundary = boundary.FindBoundary(nsIAccessibleText::BOUNDARY_WORD_START,
                                     eDirNext, flags);
  }
  // At this point, boundary is either the start of a word or at a space. A
  // word ends at the beginning of consecutive space. Therefore, skip back to
  // the start of any space before us.
  TextLeafPoint prev = boundary;
  for (;;) {
    prev = prev.FindBoundary(nsIAccessibleText::BOUNDARY_CHAR, eDirPrevious,
                             aFlags & ~BoundaryFlags::eIncludeOrigin);
    if (prev == boundary) {
      break;  // Can't go any further.
    }
    if (!prev.IsSpace()) {
      break;
    }
    boundary = prev;
  }
  return boundary;
}

TextLeafPoint TextLeafPoint::FindParagraphSameAcc(
    nsDirection aDirection, bool aIncludeOrigin,
    bool aIgnoreListItemMarker) const {
  if (aIncludeOrigin && IsDocEdge(eDirPrevious)) {
    // The top of the document is a paragraph boundary.
    return *this;
  }

  if (aIgnoreListItemMarker && aIncludeOrigin && mOffset == 0 &&
      IsLeafAfterListItemMarker()) {
    // If we are in a list item and the previous sibling is
    // a bullet, the 0 offset in this leaf is a line start.
    return *this;
  }

  if (mAcc->IsTextLeaf() &&
      // We don't want to copy strings unnecessarily. See below for the context
      // of these individual conditions.
      ((aIncludeOrigin && mOffset > 0) || aDirection == eDirNext ||
       mOffset >= 2)) {
    // If there is a line feed, a new paragraph begins after it.
    nsAutoString text;
    mAcc->AppendTextTo(text);
    if (aIncludeOrigin && mOffset > 0 && text.CharAt(mOffset - 1) == '\n') {
      return TextLeafPoint(mAcc, mOffset);
    }
    int32_t lfOffset = -1;
    if (aDirection == eDirNext) {
      lfOffset = text.FindChar('\n', mOffset);
    } else if (mOffset >= 2) {
      // A line feed at mOffset - 1 means the origin begins a new paragraph,
      // but we already handled aIncludeOrigin above. Therefore, we search from
      // mOffset - 2.
      lfOffset = text.RFindChar('\n', mOffset - 2);
    }
    if (lfOffset != -1 && lfOffset + 1 < static_cast<int32_t>(text.Length())) {
      return TextLeafPoint(mAcc, lfOffset + 1);
    }
  }

  if (aIgnoreListItemMarker && mOffset > 0 && aDirection == eDirPrevious &&
      IsLeafAfterListItemMarker()) {
    // No line breaks were found in the preceding text to this offset.
    // If we are in a list item and the previous sibling is
    // a bullet, the 0 offset in this leaf is a line start.
    return TextLeafPoint(mAcc, 0);
  }

  // Check whether this Accessible begins a paragraph.
  if ((!aIncludeOrigin && mOffset == 0) ||
      (aDirection == eDirNext && mOffset > 0)) {
    // The caller isn't interested in whether this Accessible begins a
    // paragraph.
    return TextLeafPoint();
  }
  Accessible* prevLeaf = PrevLeaf(mAcc);
  BlockRule blockRule;
  Pivot pivot(nsAccUtils::DocumentFor(mAcc));
  Accessible* prevBlock = pivot.Prev(mAcc, blockRule);
  // Check if we're the first leaf after a block element.
  if (prevBlock) {
    if (
        // If there's no previous leaf, we must be the first leaf after the
        // block.
        !prevLeaf ||
        // A block can be a leaf; e.g. an empty div or paragraph.
        prevBlock == prevLeaf) {
      return TextLeafPoint(mAcc, 0);
    }
    if (prevBlock->IsAncestorOf(mAcc)) {
      // We're inside the block.
      if (!prevBlock->IsAncestorOf(prevLeaf)) {
        // The previous leaf isn't inside the block. That means we're the first
        // leaf in the block.
        return TextLeafPoint(mAcc, 0);
      }
    } else {
      // We aren't inside the block, so the block ends before us.
      if (prevBlock->IsAncestorOf(prevLeaf)) {
        // The previous leaf is inside the block. That means we're the first
        // leaf after the block. This case is necessary because a block causes a
        // paragraph break both before and after it.
        return TextLeafPoint(mAcc, 0);
      }
    }
  }
  if (!prevLeaf || prevLeaf->IsHTMLBr()) {
    // We're the first leaf after a line break or the start of the document.
    return TextLeafPoint(mAcc, 0);
  }
  if (prevLeaf->IsTextLeaf()) {
    // There's a text leaf before us. Check if it ends with a line feed.
    nsAutoString text;
    prevLeaf->AppendTextTo(text, nsAccUtils::TextLength(prevLeaf) - 1, 1);
    if (text.CharAt(0) == '\n') {
      return TextLeafPoint(mAcc, 0);
    }
  }
  return TextLeafPoint();
}

TextLeafPoint TextLeafPoint::FindClusterSameAcc(nsDirection aDirection,
                                                bool aIncludeOrigin) const {
  // We don't support clusters which cross nodes. We can live with that because
  // editor doesn't seem to fully support this either.
  if (aIncludeOrigin && mOffset == 0) {
    // Since we don't cross nodes, offset 0 always begins a cluster.
    return *this;
  }
  if (aDirection == eDirPrevious) {
    if (mOffset == 0) {
      // We can't go back any further.
      return TextLeafPoint();
    }
    if (!aIncludeOrigin && mOffset == 1) {
      // Since we don't cross nodes, offset 0 always begins a cluster. We can't
      // take this fast path if aIncludeOrigin is true because offset 1 might
      // start a cluster, but we don't know that yet.
      return TextLeafPoint(mAcc, 0);
    }
  }
  nsAutoString text;
  mAcc->AppendTextTo(text);
  if (text.IsEmpty()) {
    return TextLeafPoint();
  }
  if (aDirection == eDirNext &&
      mOffset == static_cast<int32_t>(text.Length())) {
    return TextLeafPoint();
  }
  // There is GraphemeClusterBreakReverseIteratorUtf16, but it "doesn't
  // handle conjoining Jamo and emoji". Therefore, we must use
  // GraphemeClusterBreakIteratorUtf16 even when moving backward.
  // GraphemeClusterBreakIteratorUtf16::Seek() always starts from the beginning
  // and repeatedly calls Next(), regardless of the seek offset. The best we
  // can do is call Next() until we find the offset we need.
  intl::GraphemeClusterBreakIteratorUtf16 iter(text);
  // Since we don't cross nodes, offset 0 always begins a cluster.
  int32_t prevCluster = 0;
  while (Maybe<uint32_t> next = iter.Next()) {
    int32_t cluster = static_cast<int32_t>(*next);
    if (aIncludeOrigin && cluster == mOffset) {
      return *this;
    }
    if (aDirection == eDirPrevious) {
      if (cluster >= mOffset) {
        return TextLeafPoint(mAcc, prevCluster);
      }
      prevCluster = cluster;
    } else if (cluster > mOffset) {
      MOZ_ASSERT(aDirection == eDirNext);
      return TextLeafPoint(mAcc, cluster);
    }
  }
  return TextLeafPoint();
}

void TextLeafPoint::AddTextOffsetAttributes(AccAttributes* aAttrs) const {
  auto expose = [aAttrs](nsAtom* aAttr) {
    if (aAttr == nsGkAtoms::spelling || aAttr == nsGkAtoms::grammar) {
      // XXX We don't correctly handle exposure of overlapping spelling and
      // grammar errors. See bug 1944217. For now, we expose the one we most
      // recently encountered.
      aAttrs->SetAttribute(nsGkAtoms::invalid, aAttr);
    } else if (aAttr == nsGkAtoms::mark) {
      aAttrs->SetAttribute(aAttr, true);
    }
  };

  if (LocalAccessible* acc = mAcc->AsLocal()) {
    auto ranges = FindDOMTextOffsetAttributes(acc, mOffset, mOffset + 1);
    for (auto& [domRanges, attr] : ranges) {
      MOZ_ASSERT(domRanges.Length() >= 1);
      expose(attr);
    }
    return;
  }

  RemoteAccessible* acc = mAcc->AsRemote();
  MOZ_ASSERT(acc);
  if (RequestDomainsIfInactive(CacheDomain::TextOffsetAttributes)) {
    return;
  }
  if (!acc->mCachedFields) {
    return;
  }
  auto offsetAttrs =
      acc->mCachedFields->GetAttribute<nsTArray<TextOffsetAttribute>>(
          CacheKey::TextOffsetAttributes);
  if (!offsetAttrs) {
    return;
  }
  // offsetAttrs is sorted by mStartOffset, but ranges can overlap each other.
  // Thus, we must check all ranges with an encompassing start offset.
  for (const TextOffsetAttribute& range : *offsetAttrs) {
    if (range.mStartOffset > mOffset) {
      // offsetAttrs is sorted by mStartOffset. Therefor, there aren't any
      // ranges of interest after this.
      break;
    }
    if (range.mEndOffset != TextOffsetAttribute::kOutsideLeaf &&
        range.mEndOffset <= mOffset) {
      // range ends inside mAcc but before mOffset, so it doesn't encompass us.
      continue;
    }
    // mOffset is within range.
    expose(range.mAttribute);
  }
}

TextLeafPoint TextLeafPoint::FindTextOffsetAttributeSameAcc(
    nsDirection aDirection, bool aIncludeOrigin) const {
  if (!aIncludeOrigin && mOffset == 0 && aDirection == eDirPrevious) {
    return TextLeafPoint();
  }
  if (LocalAccessible* acc = mAcc->AsLocal()) {
    nsINode* node = acc->GetNode();
    // There are multiple selection types. The ranges for each selection type
    // are sorted, but the ranges aren't sorted between selection types.
    // Therefore, we need to look for the closest matching offset in each
    // selection type. We keep track of that in the dest variable as we check
    // each selection type.
    int32_t dest = -1;
    if (aDirection == eDirNext) {
      // We want to find both start and end points, so we pass true for
      // aAllowAdjacent.
      auto ranges = FindDOMTextOffsetAttributes(
          acc, mOffset, nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT,
          /* aAllowAdjacent */ true);
      for (auto& [domRanges, attr] : ranges) {
        for (dom::AbstractRange* domRange : domRanges) {
          if (domRange->GetStartContainer() == node) {
            int32_t matchOffset = static_cast<int32_t>(ContentToRenderedOffset(
                acc, static_cast<int32_t>(domRange->StartOffset())));
            if (aIncludeOrigin && matchOffset == mOffset) {
              return *this;
            }
            if (matchOffset > mOffset) {
              if (dest == -1 || matchOffset <= dest) {
                dest = matchOffset;
              }
              // ranges is sorted by start, so there can't be a closer range
              // offset after this. This is the only case where we can break
              // out of the loop. In the cases below, we must keep iterating
              // because the end offsets aren't sorted.
              break;
            }
          }
          if (domRange->GetEndContainer() == node) {
            int32_t matchOffset = static_cast<int32_t>(ContentToRenderedOffset(
                acc, static_cast<int32_t>(domRange->EndOffset())));
            if (aIncludeOrigin && matchOffset == mOffset) {
              return *this;
            }
            if (matchOffset > mOffset && (dest == -1 || matchOffset <= dest)) {
              dest = matchOffset;
            }
          }
        }
      }
    } else {
      auto ranges = FindDOMTextOffsetAttributes(acc, 0, mOffset,
                                                /* aAllowAdjacent */ true);
      for (auto& [domRanges, attr] : ranges) {
        for (dom::AbstractRange* domRange : Reversed(domRanges)) {
          if (domRange->GetEndContainer() == node) {
            int32_t matchOffset = static_cast<int32_t>(ContentToRenderedOffset(
                acc, static_cast<int32_t>(domRange->EndOffset())));
            if (aIncludeOrigin && matchOffset == mOffset) {
              return *this;
            }
            if (matchOffset < mOffset && (dest == -1 || matchOffset >= dest)) {
              dest = matchOffset;
            }
          }
          if (domRange->GetStartContainer() == node) {
            int32_t matchOffset = static_cast<int32_t>(ContentToRenderedOffset(
                acc, static_cast<int32_t>(domRange->StartOffset())));
            if (aIncludeOrigin && matchOffset == mOffset) {
              return *this;
            }
            if (matchOffset < mOffset && (dest == -1 || matchOffset >= dest)) {
              dest = matchOffset;
            }
          }
        }
      }
    }
    if (dest == -1) {
      return TextLeafPoint();
    }
    return TextLeafPoint(mAcc, dest);
  }

  RemoteAccessible* acc = mAcc->AsRemote();
  MOZ_ASSERT(acc);
  if (RequestDomainsIfInactive(CacheDomain::TextOffsetAttributes)) {
    return TextLeafPoint();
  }
  if (!acc->mCachedFields) {
    return TextLeafPoint();
  }
  auto offsetAttrs =
      acc->mCachedFields->GetAttribute<nsTArray<TextOffsetAttribute>>(
          CacheKey::TextOffsetAttributes);
  if (!offsetAttrs) {
    return TextLeafPoint();
  }
  // offsetAttrs is sorted by mStartOffset, but ranges can overlap each other.
  // Therefore, we must consider all ranges with an encompassing start offset.
  // An earlier range might end after a later range, so we keep track of the
  // closest offset in the dest variable and adjust that as we iterate.
  int32_t dest = -1;
  for (const TextOffsetAttribute& range : *offsetAttrs) {
    // Although range end offsets are exclusive, we must still treat them as a
    // boundary, since the end of a range still means a change in text
    // attributes and text offset attribute ranges do not have to be adjacent.
    if (aIncludeOrigin &&
        (range.mStartOffset == mOffset || range.mEndOffset == mOffset)) {
      return *this;
    }
    if (aDirection == eDirNext) {
      if (range.mStartOffset > mOffset) {
        if (dest == -1 || range.mStartOffset < dest) {
          // range.mStartOffset is the closest offset we've seen thus far.
          dest = range.mStartOffset;
        }
        // offsetAttrs is sorted by mStartOffset, so there can't be a closer
        // range offset after this.
        break;
      }
      if (range.mEndOffset > mOffset &&
          (dest == -1 || range.mEndOffset < dest)) {
        // range.mEndOffset is the closest offset we've seen thus far.
        dest = range.mEndOffset;
      }
    } else {
      if (range.mEndOffset != TextOffsetAttribute::kOutsideLeaf &&
          range.mEndOffset < mOffset && range.mEndOffset > dest) {
        // range.mEndOffset is the closest offset we've seen thus far.
        dest = range.mEndOffset;
      }
      if (range.mStartOffset >= mOffset) {
        // offsetAttrs is sorted by mStartOffset, so any range hereafter is in
        // the wrong direction.
        break;
      }
      if (range.mStartOffset != TextOffsetAttribute::kOutsideLeaf &&
          range.mStartOffset > dest) {
        // range.mStartOffset is the closest offset we've seen thus far.
        dest = range.mStartOffset;
      }
    }
  }
  if (dest == -1) {
    // There's no boundary in the requested direction.
    return TextLeafPoint();
  }
  return TextLeafPoint(mAcc, dest);
}

TextLeafPoint TextLeafPoint::NeighborLeafPoint(
    nsDirection aDirection, bool aIsEditable,
    bool aIgnoreListItemMarker) const {
  Accessible* acc = aDirection == eDirPrevious
                        ? PrevLeaf(mAcc, aIsEditable, aIgnoreListItemMarker)
                        : NextLeaf(mAcc, aIsEditable, aIgnoreListItemMarker);
  if (!acc) {
    return TextLeafPoint();
  }

  return TextLeafPoint(
      acc, aDirection == eDirPrevious
               ? static_cast<int32_t>(nsAccUtils::TextLength(acc)) - 1
               : 0);
}

LayoutDeviceIntRect TextLeafPoint::ComputeBoundsFromFrame() const {
  LocalAccessible* local = mAcc->AsLocal();
  MOZ_ASSERT(local, "Can't compute bounds in frame from non-local acc");
  nsIFrame* frame = local->GetFrame();
  MOZ_ASSERT(frame, "No frame found for acc!");

  if (!frame || !frame->IsTextFrame()) {
    return local->Bounds();
  }

  // Substring must be entirely within the same text node.
  MOZ_ASSERT(frame->IsPrimaryFrame(),
             "Cannot compute content offset on non-primary frame");
  nsIFrame::RenderedText text = frame->GetRenderedText(
      mOffset, mOffset + 1, nsIFrame::TextOffsetType::OffsetsInRenderedText,
      nsIFrame::TrailingWhitespace::DontTrim);
  int32_t contentOffset = text.mOffsetWithinNodeText;
  int32_t contentOffsetInFrame;
  // Get the right frame continuation -- not really a child, but a sibling of
  // the primary frame passed in
  nsresult rv = frame->GetChildFrameContainingOffset(
      contentOffset, true, &contentOffsetInFrame, &frame);
  NS_ENSURE_SUCCESS(rv, LayoutDeviceIntRect());

  // Start with this frame's screen rect, which we will shrink based on
  // the char we care about within it.
  nsRect frameScreenRect = frame->GetScreenRectInAppUnits();

  // Add the point where the char starts to the frameScreenRect
  nsPoint frameTextStartPoint;
  rv = frame->GetPointFromOffset(contentOffset, &frameTextStartPoint);
  NS_ENSURE_SUCCESS(rv, LayoutDeviceIntRect());

  // Use the next offset to calculate the width
  // XXX(morgan) does this work for vertical text?
  nsPoint frameTextEndPoint;
  rv = frame->GetPointFromOffset(contentOffset + 1, &frameTextEndPoint);
  NS_ENSURE_SUCCESS(rv, LayoutDeviceIntRect());

  frameScreenRect.SetRectX(
      frameScreenRect.X() +
          std::min(frameTextStartPoint.x, frameTextEndPoint.x),
      mozilla::Abs(frameTextStartPoint.x - frameTextEndPoint.x));

  nsPresContext* presContext = local->Document()->PresContext();
  return LayoutDeviceIntRect::FromAppUnitsToNearest(
      frameScreenRect, presContext->AppUnitsPerDevPixel());
}

/* static */
nsTArray<TextOffsetAttribute> TextLeafPoint::GetTextOffsetAttributes(
    LocalAccessible* aAcc) {
  nsINode* node = aAcc->GetNode();
  auto ranges = FindDOMTextOffsetAttributes(
      aAcc, 0, nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT);
  size_t capacity = 0;
  for (auto& [domRanges, attr] : ranges) {
    capacity += domRanges.Length();
  }
  nsTArray<TextOffsetAttribute> offsets(capacity);
  for (auto& [domRanges, attr] : ranges) {
    for (dom::AbstractRange* domRange : domRanges) {
      TextOffsetAttribute& data = *offsets.AppendElement();
      data.mAttribute = attr;
      if (domRange->GetStartContainer() == node) {
        data.mStartOffset = static_cast<int32_t>(ContentToRenderedOffset(
            aAcc, static_cast<int32_t>(domRange->StartOffset())));
      } else {
        // This range overlaps aAcc, but starts before it.
        // This can only happen for the first range.
        MOZ_ASSERT(domRange == *domRanges.begin());
        data.mStartOffset = TextOffsetAttribute::kOutsideLeaf;
      }
      if (domRange->GetEndContainer() == node) {
        data.mEndOffset = static_cast<int32_t>(ContentToRenderedOffset(
            aAcc, static_cast<int32_t>(domRange->EndOffset())));
      } else {
        // This range overlaps aAcc, but ends after it.
        // This can only happen for the last range.
        MOZ_ASSERT(domRange == *domRanges.rbegin());
        data.mEndOffset = TextOffsetAttribute::kOutsideLeaf;
      }
    }
  }
  offsets.Sort();
  return offsets;
}

/* static */
void TextLeafPoint::UpdateCachedTextOffsetAttributes(
    dom::Document* aDocument, const dom::AbstractRange& aRange) {
  DocAccessible* docAcc = GetExistingDocAccessible(aDocument);
  if (!docAcc) {
    return;
  }
  LocalAccessible* startAcc = docAcc->GetAccessible(aRange.GetStartContainer());
  LocalAccessible* endAcc = docAcc->GetAccessible(aRange.GetEndContainer());
  if (!startAcc || !endAcc) {
    return;
  }
  for (Accessible* acc = startAcc; acc; acc = NextLeaf(acc)) {
    if (acc->IsTextLeaf()) {
      docAcc->QueueCacheUpdate(acc->AsLocal(),
                               CacheDomain::TextOffsetAttributes);
    }
    if (acc == endAcc) {
      // Subtle: We check this here rather than in the loop condition because
      // we want to include endAcc but stop once we reach it. Putting it in the
      // loop condition would mean we stop at endAcc, but we would also exclude
      // it; i.e. we wouldn't push the cache for it.
      break;
    }
  }
}

already_AddRefed<AccAttributes> TextLeafPoint::GetTextAttributesLocalAcc(
    bool aIncludeDefaults) const {
  LocalAccessible* acc = mAcc->AsLocal();
  MOZ_ASSERT(acc);
  MOZ_ASSERT(acc->IsText());
  // TextAttrsMgr wants a HyperTextAccessible.
  LocalAccessible* parent = acc->LocalParent();
  if (!parent) {
    // This should only happen if a client query occurs after a hide event is
    // queued for acc and after acc is detached from the document, but before
    // the event is fired and thus before acc is shut down.
    MOZ_ASSERT(!acc->IsInDocument());
    return nullptr;
  }
  HyperTextAccessible* hyperAcc = parent->AsHyperText();
  MOZ_ASSERT(hyperAcc);
  RefPtr<AccAttributes> attributes = new AccAttributes();
  if (hyperAcc) {
    TextAttrsMgr mgr(hyperAcc, aIncludeDefaults, acc);
    mgr.GetAttributes(attributes);
  }
  return attributes.forget();
}

already_AddRefed<AccAttributes> TextLeafPoint::GetTextAttributes(
    bool aIncludeDefaults) const {
  if (!mAcc->IsText()) {
    return nullptr;
  }
  RefPtr<AccAttributes> attrs;
  if (mAcc->IsLocal()) {
    attrs = GetTextAttributesLocalAcc(aIncludeDefaults);
  } else {
    attrs = new AccAttributes();
    if (aIncludeDefaults) {
      Accessible* parent = mAcc->Parent();
      if (parent && parent->IsRemote() && parent->IsHyperText()) {
        if (auto defAttrs = parent->AsRemote()->GetCachedTextAttributes()) {
          defAttrs->CopyTo(attrs);
        }
      }
    }
    if (auto thisAttrs = mAcc->AsRemote()->GetCachedTextAttributes()) {
      thisAttrs->CopyTo(attrs);
    }
  }
  AddTextOffsetAttributes(attrs);
  return attrs.forget();
}

TextLeafPoint TextLeafPoint::FindTextAttrsStart(nsDirection aDirection,
                                                bool aIncludeOrigin) const {
  if (mIsEndOfLineInsertionPoint) {
    return AdjustEndOfLine().FindTextAttrsStart(aDirection, aIncludeOrigin);
  }
  const bool isRemote = mAcc->IsRemote();
  RefPtr<const AccAttributes> lastAttrs;
  if (mAcc->IsText()) {
    lastAttrs = isRemote ? mAcc->AsRemote()->GetCachedTextAttributes()
                         : GetTextAttributesLocalAcc();
  }
  if (aIncludeOrigin && aDirection == eDirNext && mOffset == 0) {
    if (!mAcc->IsText()) {
      // Anything other than text breaks an attrs run.
      return *this;
    }
    // Even when searching forward, the only way to know whether the origin is
    // the start of a text attrs run is to compare with the previous sibling.
    TextLeafPoint point;
    point.mAcc = mAcc->PrevSibling();
    if (!point.mAcc || !point.mAcc->IsText()) {
      return *this;
    }
    // For RemoteAccessible, we can get attributes from the cache without any
    // calculation or copying.
    RefPtr<const AccAttributes> attrs =
        isRemote ? point.mAcc->AsRemote()->GetCachedTextAttributes()
                 : point.GetTextAttributesLocalAcc();
    if (attrs && lastAttrs && !attrs->Equal(lastAttrs)) {
      return *this;
    }
  }
  TextLeafPoint lastPoint = *this;
  // If we're at the start of the container and searching for a previous start,
  // start the search from the previous leaf. Otherwise, we'll miss the previous
  // start.
  const bool shouldTraversePrevLeaf = [&]() {
    const bool shouldTraverse =
        !aIncludeOrigin && aDirection == eDirPrevious && mOffset == 0;
    Accessible* prevSibling = mAcc->PrevSibling();
    if (prevSibling) {
      return shouldTraverse && !prevSibling->IsText();
    }
    return shouldTraverse;
  }();
  if (shouldTraversePrevLeaf) {
    // Go to the previous leaf and start the search from there, if it exists.
    Accessible* prevLeaf = PrevLeaf(mAcc);
    if (!prevLeaf) {
      return *this;
    }
    lastPoint = TextLeafPoint(
        prevLeaf, static_cast<int32_t>(nsAccUtils::TextLength(prevLeaf)));
  }
  // This loop searches within a container (that is, it only looks at siblings).
  // We might cross containers before or after this loop, but not within it.
  for (;;) {
    if (TextLeafPoint offsetAttr = lastPoint.FindTextOffsetAttributeSameAcc(
            aDirection, aIncludeOrigin && lastPoint.mAcc == mAcc)) {
      // An offset attribute starts or ends somewhere in the Accessible we're
      // considering. This causes an attribute change, so return that point.
      return offsetAttr;
    }
    TextLeafPoint point;
    point.mAcc = aDirection == eDirNext ? lastPoint.mAcc->NextSibling()
                                        : lastPoint.mAcc->PrevSibling();
    if (!point.mAcc || !point.mAcc->IsText()) {
      break;
    }
    RefPtr<const AccAttributes> attrs =
        isRemote ? point.mAcc->AsRemote()->GetCachedTextAttributes()
                 : point.GetTextAttributesLocalAcc();
    if (!lastAttrs || (attrs && !attrs->Equal(lastAttrs))) {
      // The attributes change here. If we're moving forward, we want to return
      // this point.
      if (aDirection == eDirNext) {
        return point;
      }

      // Otherwise, we're moving backward and we've now moved before the start
      // point of the current text attributes run.
      const auto attrsStart = TextLeafPoint(lastPoint.mAcc, 0);

      // Return the current text attributes run start point if:
      //   1. The caller wants this function to include the origin in the
      //   search (aIncludeOrigin implies that we must return the first text
      //   attributes run start point that we find, even if that point is the
      //   origin)
      //   2. Our search did not begin on the text attributes run start point
      if (aIncludeOrigin || attrsStart != *this) {
        return attrsStart;
      }

      // Otherwise, the origin was the attributes run start point and the caller
      // wants this function to ignore it in its search. Keep searching.
    }
    lastPoint = point;
    if (aDirection == eDirPrevious) {
      // On the next iteration, we want to search for offset attributes from the
      // end of this Accessible.
      lastPoint.mOffset =
          static_cast<int32_t>(nsAccUtils::TextLength(point.mAcc));
    }
    lastAttrs = attrs;
  }

  // We couldn't move any further in this container.
  if (aDirection == eDirPrevious) {
    // Treat the start of a container as a format boundary.
    return TextLeafPoint(lastPoint.mAcc, 0);
  }
  // If we're at the end of the container then we have to use the start of the
  // next leaf.
  Accessible* nextLeaf = NextLeaf(lastPoint.mAcc);
  if (nextLeaf) {
    return TextLeafPoint(nextLeaf, 0);
  }
  // If there's no next leaf, then fall back to the end of the last point.
  return TextLeafPoint(
      lastPoint.mAcc,
      static_cast<int32_t>(nsAccUtils::TextLength(lastPoint.mAcc)));
}

LayoutDeviceIntRect TextLeafPoint::CharBounds() {
  if (!mAcc) {
    return LayoutDeviceIntRect();
  }

  if (mAcc->IsHTMLBr()) {
    // HTML <br> elements don't provide character bounds, but do provide text (a
    // line feed). They also have 0 width and/or height, depending on the
    // doctype and writing mode. Expose minimum 1 x 1 so clients treat it as an
    // actual rectangle; e.g. when the caret is positioned on a <br>.
    LayoutDeviceIntRect bounds = mAcc->Bounds();
    if (bounds.width == 0) {
      bounds.width = 1;
    }
    if (bounds.height == 0) {
      bounds.height = 1;
    }
    return bounds;
  }

  if (!mAcc->IsTextLeaf()) {
    // This could be an empty container. Alternatively, it could be a list
    // bullet,which does provide text but doesn't support character bounds. In
    // either case, return the Accessible's bounds.
    return mAcc->Bounds();
  }

  auto maybeAdjustLineFeedBounds = [this](LayoutDeviceIntRect& aBounds) {
    if (!IsLineFeedChar()) {
      return;
    }
    // Line feeds have a 0 width or height, depending on the writing mode.
    // Use 1 instead so that clients treat it as an actual rectangle; e.g. when
    // displaying the caret when it is positioned on a line feed.
    MOZ_ASSERT(aBounds.IsZeroArea());
    if (aBounds.width == 0) {
      aBounds.width = 1;
    }
    if (aBounds.height == 0) {
      aBounds.height = 1;
    }
  };

  if (LocalAccessible* local = mAcc->AsLocal()) {
    if (mOffset >= 0 &&
        static_cast<uint32_t>(mOffset) >= nsAccUtils::TextLength(local)) {
      // It's valid for a caller to query the length because the caret might be
      // at the end of editable text. In that case, we should just silently
      // return. However, we assert that the offset isn't greater than the
      // length.
      NS_ASSERTION(
          static_cast<uint32_t>(mOffset) <= nsAccUtils::TextLength(local),
          "Wrong in offset");
      return LayoutDeviceIntRect();
    }

    LayoutDeviceIntRect bounds = ComputeBoundsFromFrame();

    // This document may have a resolution set, we will need to multiply
    // the document-relative coordinates by that value and re-apply the doc's
    // screen coordinates.
    nsPresContext* presContext = local->Document()->PresContext();
    nsIFrame* rootFrame = presContext->PresShell()->GetRootFrame();
    LayoutDeviceIntRect orgRectPixels =
        LayoutDeviceIntRect::FromAppUnitsToNearest(
            rootFrame->GetScreenRectInAppUnits(),
            presContext->AppUnitsPerDevPixel());
    bounds.MoveBy(-orgRectPixels.X(), -orgRectPixels.Y());
    bounds.ScaleRoundOut(presContext->PresShell()->GetResolution());
    bounds.MoveBy(orgRectPixels.X(), orgRectPixels.Y());
    maybeAdjustLineFeedBounds(bounds);
    return bounds;
  }

  if (RequestDomainsIfInactive(CacheDomain::TextBounds)) {
    return LayoutDeviceIntRect();
  }
  RemoteAccessible* remote = mAcc->AsRemote();
  if (!remote->mCachedFields) {
    return LayoutDeviceIntRect();
  }

  nsRect charBounds = remote->GetCachedCharRect(mOffset);
  // A character can have 0 width, but we still want its other coordinates.
  // Thus, we explicitly test for an all-0 rect here to determine whether this
  // is a valid char rect, rather than using IsZeroArea or IsEmpty.
  if (!charBounds.IsEqualRect(0, 0, 0, 0)) {
    LayoutDeviceIntRect bounds = remote->BoundsWithOffset(Some(charBounds));
    maybeAdjustLineFeedBounds(bounds);
    return bounds;
  }

  return LayoutDeviceIntRect();
}

bool TextLeafPoint::ContainsPoint(int32_t aX, int32_t aY) {
  if (mAcc && !mAcc->IsText()) {
    // If we're dealing with an empty embedded object, use the
    // accessible's non-text bounds.
    return mAcc->Bounds().Contains(aX, aY);
  }

  return CharBounds().Contains(aX, aY);
}

/*** TextLeafRange ***/

bool TextLeafRange::Crop(Accessible* aContainer) {
  TextLeafPoint containerStart(aContainer, 0);
  TextLeafPoint containerEnd(aContainer,
                             nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT);

  if (mEnd < containerStart || containerEnd < mStart) {
    // The range ends before the container, or starts after it.
    return false;
  }

  if (mStart < containerStart) {
    // If range start is before container start, adjust range start to
    // start of container.
    mStart = containerStart;
  }

  if (containerEnd < mEnd) {
    // If range end is after container end, adjust range end to end of
    // container.
    mEnd = containerEnd;
  }

  return true;
}

LayoutDeviceIntRect TextLeafRange::Bounds() const {
  // We can't simply query the first and last character, and union their bounds.
  // They might reside on different lines, so a simple union may yield an
  // incorrect width. We should use the length of the longest spanned line for
  // our width. To achieve this, walk all the lines and union them into the
  // result rectangle.
  LayoutDeviceIntRect result = TextLeafPoint{mStart}.CharBounds();
  const bool succeeded = WalkLineRects(
      [&result](TextLeafRange aLine, LayoutDeviceIntRect aLineRect) {
        result.UnionRect(result, aLineRect);
      });

  if (!succeeded) {
    return {};
  }
  return result;
}

nsTArray<LayoutDeviceIntRect> TextLeafRange::LineRects() const {
  // Get the bounds of the content so we can restrict our lines to just the
  // text visible within the bounds of the document.
  Maybe<LayoutDeviceIntRect> contentBounds;
  if (Accessible* doc = nsAccUtils::DocumentFor(mStart.mAcc)) {
    contentBounds.emplace(doc->Bounds());
  }

  nsTArray<LayoutDeviceIntRect> lineRects;
  WalkLineRects([&lineRects, &contentBounds](TextLeafRange aLine,
                                             LayoutDeviceIntRect aLineRect) {
    // Clip the bounds to the bounds of the content area.
    bool boundsVisible = true;
    if (contentBounds.isSome()) {
      boundsVisible = aLineRect.IntersectRect(aLineRect, *contentBounds);
    }
    if (boundsVisible) {
      lineRects.AppendElement(aLineRect);
    }
  });

  return lineRects;
}

TextLeafPoint TextLeafRange::TextLeafPointAtScreenPoint(int32_t aX,
                                                        int32_t aY) const {
  // Step backwards one character to make the endPoint inclusive. This means we
  // can use operator!= when comparing against endPoint below (which is very
  // fast), rather than operator< (which might be significantly slower).
  const TextLeafPoint endPoint =
      mEnd.FindBoundary(nsIAccessibleText::BOUNDARY_CHAR, eDirPrevious);

  // If there are no characters in this container, we might have moved endPoint
  // before mStart. In that case, we shouldn't try to move farther forward, as
  // that might result in an infinite loop.
  TextLeafPoint point = mStart;
  if (mStart <= endPoint) {
    for (; !point.ContainsPoint(aX, aY) && point != endPoint;
         point =
             point.FindBoundary(nsIAccessibleText::BOUNDARY_CHAR, eDirNext)) {
    }
  }

  return point;
}

bool TextLeafRange::SetSelection(int32_t aSelectionNum, bool aSetFocus) const {
  if (!mStart || !mEnd || mStart.mAcc->IsLocal() != mEnd.mAcc->IsLocal()) {
    return false;
  }

  if (mStart.mAcc->IsRemote()) {
    DocAccessibleParent* doc = mStart.mAcc->AsRemote()->Document();
    if (doc != mEnd.mAcc->AsRemote()->Document()) {
      return false;
    }

    Unused << doc->SendSetTextSelection(mStart.mAcc->ID(), mStart.mOffset,
                                        mEnd.mAcc->ID(), mEnd.mOffset,
                                        aSelectionNum, aSetFocus);
    return true;
  }

  bool reversed = mEnd < mStart;
  auto [startContent, startContentOffset] =
      !reversed ? mStart.ToDOMPoint(false) : mEnd.ToDOMPoint(false);
  auto [endContent, endContentOffset] =
      !reversed ? mEnd.ToDOMPoint(false) : mStart.ToDOMPoint(false);
  if (!startContent || !endContent) {
    return false;
  }

  RefPtr<dom::Selection> domSel = GetDOMSelection(startContent, endContent);
  if (!domSel) {
    return false;
  }

  HyperTextAccessible* hyp = nullptr;
  if (mStart.mAcc->IsHyperText()) {
    hyp = mStart.mAcc->AsLocal()->AsHyperText();
  } else {
    Accessible* parent = mStart.mAcc->Parent();
    if (parent) {
      hyp = parent->AsLocal()->AsHyperText();
      // Note that hyp will still be null here if the parent is not a HyperText.
      // That's okay.
    }
  }

  // Before setting the selection range, we need to ensure that the editor
  // is initialized. (See bug 804927.)
  // Otherwise, it's possible that lazy editor initialization will override
  // the selection we set here and leave the caret at the end of the text.
  // By calling GetEditor here, we ensure that editor initialization is
  // completed before we set the selection.
  RefPtr<EditorBase> editor;
  if (hyp) {
    editor = hyp->GetEditor();
  }

  // XXX isFocusable will be false if mStart is not a direct child of the
  // contentEditable. However, contentEditables generally don't mess with
  // selection when they are focused. This has also been our behavior for a very
  // long time.
  const bool isFocusable = hyp && hyp->InteractiveState() & states::FOCUSABLE;
  // If the Accessible is focusable, focus it before setting the selection to
  // override the control's own selection changes on focus if any; e.g. inputs
  // that do select all on focus. This also ensures that the user can interact
  // with wherever they've moved the caret. See bug 524115.
  if (aSetFocus && isFocusable) {
    hyp->TakeFocus();
  }

  uint32_t rangeCount = 0;
  if (aSelectionNum == kRemoveAllExistingSelectedRanges) {
    domSel->RemoveAllRanges(IgnoreErrors());
  } else {
    rangeCount = domSel->RangeCount();
  }
  RefPtr<nsRange> domRange = nullptr;
  const bool newRange =
      aSelectionNum == static_cast<int32_t>(rangeCount) || aSelectionNum < 0;
  if (newRange) {
    domRange = nsRange::Create(startContent);
  } else {
    domRange = domSel->GetRangeAt(AssertedCast<uint32_t>(aSelectionNum));
  }
  if (!domRange) {
    return false;
  }

  domRange->SetStart(startContent, startContentOffset);
  domRange->SetEnd(endContent, endContentOffset);

  // If this is not a new range, notify selection listeners that the existing
  // selection range has changed. Otherwise, just add the new range.
  if (!newRange) {
    domSel->RemoveRangeAndUnselectFramesAndNotifyListeners(*domRange,
                                                           IgnoreErrors());
  }

  IgnoredErrorResult err;
  domSel->AddRangeAndSelectFramesAndNotifyListeners(*domRange, err);
  if (err.Failed()) {
    return false;
  }

  // Changing the direction of the selection assures that the caret
  // will be at the logical end of the selection.
  domSel->SetDirection(reversed ? eDirPrevious : eDirNext);

  // Make sure the selection is visible. See bug 1170242.
  domSel->ScrollIntoView(nsISelectionController::SELECTION_FOCUS_REGION,
                         ScrollAxis(), ScrollAxis(),
                         ScrollFlags::ScrollOverflowHidden);

  if (aSetFocus && mStart == mEnd && !isFocusable) {
    // We're moving the caret. Notify nsFocusManager so that the focus position
    // is correct. See bug 546068.
    if (nsFocusManager* DOMFocusManager = nsFocusManager::GetFocusManager()) {
      MOZ_ASSERT(mStart.mAcc->AsLocal()->Document());
      dom::Document* domDoc =
          mStart.mAcc->AsLocal()->Document()->DocumentNode();
      MOZ_ASSERT(domDoc);
      nsCOMPtr<nsPIDOMWindowOuter> window = domDoc->GetWindow();
      RefPtr<dom::Element> result;
      DOMFocusManager->MoveFocus(
          window, nullptr, nsIFocusManager::MOVEFOCUS_CARET,
          nsIFocusManager::FLAG_BYMOVEFOCUS, getter_AddRefs(result));
    }
  }
  return true;
}

/* static */
void TextLeafRange::GetSelection(Accessible* aAcc,
                                 nsTArray<TextLeafRange>& aRanges) {
  // Use HyperTextAccessibleBase::SelectionRanges. Eventually, we'll want to
  // move that code into TextLeafPoint, but events and caching are based on
  // HyperText offsets for now.
  HyperTextAccessibleBase* hyp = aAcc->AsHyperTextBase();
  if (!hyp) {
    return;
  }
  AutoTArray<TextRange, 1> hypRanges;
  hyp->CroppedSelectionRanges(hypRanges);
  aRanges.SetCapacity(hypRanges.Length());
  for (TextRange& hypRange : hypRanges) {
    TextLeafPoint start =
        hypRange.StartContainer()->AsHyperTextBase()->ToTextLeafPoint(
            hypRange.StartOffset());
    TextLeafPoint end =
        hypRange.EndContainer()->AsHyperTextBase()->ToTextLeafPoint(
            hypRange.EndOffset());
    aRanges.EmplaceBack(start, end);
  }
}

void TextLeafRange::ScrollIntoView(uint32_t aScrollType) const {
  if (!mStart || !mEnd || mStart.mAcc->IsLocal() != mEnd.mAcc->IsLocal()) {
    return;
  }

  if (mStart.mAcc->IsRemote()) {
    DocAccessibleParent* doc = mStart.mAcc->AsRemote()->Document();
    if (doc != mEnd.mAcc->AsRemote()->Document()) {
      // Can't scroll range that spans docs.
      return;
    }

    Unused << doc->SendScrollTextLeafRangeIntoView(
        mStart.mAcc->ID(), mStart.mOffset, mEnd.mAcc->ID(), mEnd.mOffset,
        aScrollType);
    return;
  }

  auto [startContent, startContentOffset] = mStart.ToDOMPoint();
  auto [endContent, endContentOffset] = mEnd.ToDOMPoint();

  if (!startContent || !endContent) {
    return;
  }

  ErrorResult er;
  RefPtr<nsRange> domRange = nsRange::Create(startContent, startContentOffset,
                                             endContent, endContentOffset, er);
  if (er.Failed()) {
    return;
  }

  nsCoreUtils::ScrollSubstringTo(mStart.mAcc->AsLocal()->GetFrame(), domRange,
                                 aScrollType);
}

nsTArray<TextLeafRange> TextLeafRange::VisibleLines(
    Accessible* aContainer) const {
  MOZ_ASSERT(aContainer);
  // We want to restrict our lines to those visible within aContainer.
  LayoutDeviceIntRect containerBounds = aContainer->Bounds();
  nsTArray<TextLeafRange> lines;
  WalkLineRects([&lines, &containerBounds](TextLeafRange aLine,
                                           LayoutDeviceIntRect aLineRect) {
    // XXX This doesn't correctly handle lines that are scrolled out where the
    // scroll container is a descendant of aContainer. Such lines might
    // intersect with containerBounds, but the scroll container could be a
    // descendant of aContainer and should thus exclude this line. See bug
    // 1945010 for more details.
    if (aLineRect.Intersects(containerBounds)) {
      lines.AppendElement(aLine);
    }
  });
  return lines;
}

bool TextLeafRange::WalkLineRects(LineRectCallback aCallback) const {
  if (mEnd <= mStart) {
    return false;
  }

  bool locatedFinalLine = false;
  TextLeafPoint currPoint = mStart;

  // Union the first and last chars of each line to create a line rect.
  while (!locatedFinalLine) {
    TextLeafPoint nextLineStartPoint = currPoint.FindBoundary(
        nsIAccessibleText::BOUNDARY_LINE_START, eDirNext);
    // If currPoint is at the end of the document, nextLineStartPoint will be
    // equal to currPoint. However, this can only happen if mEnd is also the end
    // of the document.
    MOZ_ASSERT(nextLineStartPoint != currPoint || nextLineStartPoint == mEnd);
    if (mEnd <= nextLineStartPoint) {
      // nextLineStart is past the end of the range. Constrain this last line to
      // the end of the range.
      nextLineStartPoint = mEnd;
      locatedFinalLine = true;
    }
    // Fetch the last point in the current line by going back one char from the
    // start of the next line.
    TextLeafPoint lastPointInLine = nextLineStartPoint.FindBoundary(
        nsIAccessibleText::BOUNDARY_CHAR, eDirPrevious);
    MOZ_ASSERT(currPoint <= lastPointInLine);

    LayoutDeviceIntRect currLineRect = currPoint.CharBounds();
    currLineRect.UnionRect(currLineRect, lastPointInLine.CharBounds());
    // The range we pass must include the last character and range ends are
    // exclusive, hence the use of nextLineStartPoint.
    TextLeafRange currLine = TextLeafRange(currPoint, nextLineStartPoint);
    aCallback(currLine, currLineRect);

    currPoint = nextLineStartPoint;
  }
  return true;
}

TextLeafRange::Iterator TextLeafRange::Iterator::BeginIterator(
    const TextLeafRange& aRange) {
  Iterator result(aRange);

  result.mSegmentStart = aRange.mStart;
  if (aRange.mStart.mAcc == aRange.mEnd.mAcc) {
    result.mSegmentEnd = aRange.mEnd;
  } else {
    result.mSegmentEnd = TextLeafPoint(
        aRange.mStart.mAcc, nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT);
  }

  return result;
}

TextLeafRange::Iterator TextLeafRange::Iterator::EndIterator(
    const TextLeafRange& aRange) {
  Iterator result(aRange);

  result.mSegmentEnd = TextLeafPoint();
  result.mSegmentStart = TextLeafPoint();

  return result;
}

TextLeafRange::Iterator& TextLeafRange::Iterator::operator++() {
  if (mSegmentEnd.mAcc == mRange.mEnd.mAcc) {
    mSegmentEnd = TextLeafPoint();
    mSegmentStart = TextLeafPoint();
    return *this;
  }

  if (Accessible* nextLeaf = NextLeaf(mSegmentEnd.mAcc)) {
    mSegmentStart = TextLeafPoint(nextLeaf, 0);
    if (nextLeaf == mRange.mEnd.mAcc) {
      mSegmentEnd = TextLeafPoint(nextLeaf, mRange.mEnd.mOffset);
    } else {
      mSegmentEnd =
          TextLeafPoint(nextLeaf, nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT);
    }
  } else {
    mSegmentEnd = TextLeafPoint();
    mSegmentStart = TextLeafPoint();
  }

  return *this;
}

}  // namespace mozilla::a11y
