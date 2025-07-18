/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _LocalAccessible_H_
#define _LocalAccessible_H_

#include "mozilla/ComputedStyle.h"
#include "mozilla/a11y/Accessible.h"
#include "mozilla/a11y/AccTypes.h"
#include "mozilla/a11y/CacheConstants.h"
#include "mozilla/a11y/RelationType.h"
#include "mozilla/a11y/States.h"

#include "mozilla/UniquePtr.h"

#include "nsIContent.h"
#include "nsTArray.h"
#include "nsRefPtrHashtable.h"
#include "nsRect.h"

struct nsRoleMapEntry;

class nsIFrame;

class nsAttrValue;

namespace mozilla::dom {
class Element;
}

namespace mozilla {
namespace a11y {

class LocalAccessible;
class AccAttributes;
class AccEvent;
class AccGroupInfo;
class ApplicationAccessible;
class CacheData;
class DocAccessible;
class EmbeddedObjCollector;
class EventTree;
class HTMLImageMapAccessible;
class HTMLLIAccessible;
class HTMLLinkAccessible;
class HyperTextAccessible;
class HyperTextAccessibleBase;
class ImageAccessible;
class KeyBinding;
class OuterDocAccessible;
class RemoteAccessible;
class Relation;
class RootAccessible;
class TableAccessible;
class TableCellAccessible;
class TextLeafAccessible;
class XULLabelAccessible;
class XULTreeAccessible;

enum class CacheUpdateType;

#ifdef A11Y_LOG
namespace logging {
typedef const char* (*GetTreePrefix)(void* aData, LocalAccessible*);
void Tree(const char* aTitle, const char* aMsgText, LocalAccessible* aRoot,
          GetTreePrefix aPrefixFunc, void* GetTreePrefixData);
void TreeSize(const char* aTitle, const char* aMsgText, LocalAccessible* aRoot);
};  // namespace logging
#endif

typedef nsRefPtrHashtable<nsPtrHashKey<const void>, LocalAccessible>
    AccessibleHashtable;

#define NS_ACCESSIBLE_IMPL_IID                \
  {/* 133c8bf4-4913-4355-bd50-426bd1d6e1ad */ \
   0x133c8bf4,                                \
   0x4913,                                    \
   0x4355,                                    \
   {0xbd, 0x50, 0x42, 0x6b, 0xd1, 0xd6, 0xe1, 0xad}}

/**
 * An accessibility tree node that originated in mDoc's content process.
 */
class LocalAccessible : public nsISupports, public Accessible {
 public:
  LocalAccessible(nsIContent* aContent, DocAccessible* aDoc);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(LocalAccessible)

  NS_INLINE_DECL_STATIC_IID(NS_ACCESSIBLE_IMPL_IID)

  //////////////////////////////////////////////////////////////////////////////
  // Public methods

  /**
   * Return the document accessible for this accessible.
   */
  DocAccessible* Document() const { return mDoc; }

  /**
   * Return the root document accessible for this accessible.
   */
  a11y::RootAccessible* RootAccessible() const;

  /**
   * Return frame for this accessible.
   * Note that this will return null for display: contents. Also,
   * DocAccessible::GetFrame can return null if the frame tree hasn't been
   * created yet.
   */
  virtual nsIFrame* GetFrame() const;

  /**
   * Return DOM node associated with the accessible.
   */
  virtual nsINode* GetNode() const;

  nsIContent* GetContent() const { return mContent; }
  dom::Element* Elm() const;

  /**
   * Return node type information of DOM node associated with the accessible.
   */
  bool IsContent() const { return GetNode() && GetNode()->IsContent(); }

  /**
   * Return the unique identifier of the accessible.
   * ID() should be preferred, but this method still exists because many
   * LocalAccessible callers expect a void*.
   */
  void* UniqueID() { return static_cast<void*>(this); }

  virtual uint64_t ID() const override {
    return IsDoc() ? 0 : reinterpret_cast<uintptr_t>(this);
  }

  virtual void Language(nsAString& aLocale) override;

  /**
   * Get the description of this accessible.
   */
  virtual void Description(nsString& aDescription) const override;

  /**
   * Get the value of this accessible.
   */
  virtual void Value(nsString& aValue) const override;

  /**
   * Get the name of this accessible.
   */
  virtual ENameValueFlag Name(nsString& aName) const override;

  /**
   * Maps ARIA state attributes to state of accessible. Note the given state
   * argument should hold states for accessible before you pass it into this
   * method.
   *
   * @param  [in/out] where to fill the states into.
   */
  virtual void ApplyARIAState(uint64_t* aState) const;

  /**
   * Return enumerated accessible role (see constants in Role.h).
   */
  virtual mozilla::a11y::role Role() const override;

  /**
   * Return accessible role specified by ARIA (see constants in
   * roles).
   */
  inline mozilla::a11y::role ARIARole();

  /**
   * Returns enumerated accessible role from native markup (see constants in
   * Role.h). Doesn't take into account ARIA roles.
   */
  virtual mozilla::a11y::role NativeRole() const;

  virtual uint64_t State() override;

  /**
   * Return interactive states present on the accessible
   * (@see NativeInteractiveState).
   */
  uint64_t InteractiveState() const {
    uint64_t state = NativeInteractiveState();
    ApplyARIAState(&state);
    return state;
  }

  /**
   * Return link states present on the accessible.
   */
  uint64_t LinkState() const {
    uint64_t state = NativeLinkState();
    ApplyARIAState(&state);
    return state;
  }

  /**
   * Return the states of accessible, not taking into account ARIA states.
   * Use State() to get complete set of states.
   */
  virtual uint64_t NativeState() const;

  /**
   * Return native interactice state (unavailable, focusable or selectable).
   */
  virtual uint64_t NativeInteractiveState() const;

  /**
   * Return native link states present on the accessible.
   */
  virtual uint64_t NativeLinkState() const;

  /**
   * Return bit set of invisible and offscreen states.
   */
  uint64_t VisibilityState() const;

  /**
   * Return true if native unavailable state present.
   */
  virtual bool NativelyUnavailable() const;

  virtual already_AddRefed<AccAttributes> Attributes() override;

  /**
   * Return direct or deepest child at the given point.
   *
   * @param  aX           [in] x coordinate relative screen
   * @param  aY           [in] y coordinate relative screen
   * @param  aWhichChild  [in] flag points if deepest or direct child
   *                        should be returned
   */
  virtual LocalAccessible* LocalChildAtPoint(int32_t aX, int32_t aY,
                                             EWhichChildAtPoint aWhichChild);

  /**
   * Similar to LocalChildAtPoint but crosses process boundaries.
   */
  virtual Accessible* ChildAtPoint(int32_t aX, int32_t aY,
                                   EWhichChildAtPoint aWhichChild) override;

  virtual Relation RelationByType(RelationType aType) const override;

  //////////////////////////////////////////////////////////////////////////////
  // Initializing methods

  /**
   * Shutdown this accessible object.
   */
  virtual void Shutdown();

  /**
   * Set the ARIA role map entry for a new accessible.
   */
  inline void SetRoleMapEntry(const nsRoleMapEntry* aRoleMapEntry);

  /**
   * Append/insert/remove a child. Return true if operation was successful.
   */
  bool AppendChild(LocalAccessible* aChild) {
    return InsertChildAt(mChildren.Length(), aChild);
  }
  virtual bool InsertChildAt(uint32_t aIndex, LocalAccessible* aChild);

  /**
   * Inserts a child after given sibling. If the child cannot be inserted,
   * then the child is unbound from the document, and false is returned. Make
   * sure to null out any references on the child object as it may be destroyed.
   */
  inline bool InsertAfter(LocalAccessible* aNewChild,
                          LocalAccessible* aRefChild);

  virtual bool RemoveChild(LocalAccessible* aChild);

  /**
   * Reallocates the child within its parent.
   */
  virtual void RelocateChild(uint32_t aNewIndex, LocalAccessible* aChild);

  // Accessible hierarchy method overrides

  virtual Accessible* Parent() const override { return LocalParent(); }

  virtual Accessible* ChildAt(uint32_t aIndex) const override {
    return LocalChildAt(aIndex);
  }

  virtual Accessible* NextSibling() const override {
    return LocalNextSibling();
  }

  virtual Accessible* PrevSibling() const override {
    return LocalPrevSibling();
  }

  //////////////////////////////////////////////////////////////////////////////
  // LocalAccessible tree traverse methods

  /**
   * Return parent accessible.
   */
  LocalAccessible* LocalParent() const { return mParent; }

  /**
   * Return child accessible at the given index.
   */
  virtual LocalAccessible* LocalChildAt(uint32_t aIndex) const;

  /**
   * Return child accessible count.
   */
  virtual uint32_t ChildCount() const override;

  /**
   * Return index of the given child accessible.
   */
  int32_t GetIndexOf(const LocalAccessible* aChild) const {
    return (aChild->mParent != this) ? -1 : aChild->IndexInParent();
  }

  /**
   * Return index in parent accessible.
   */
  virtual int32_t IndexInParent() const override;

  /**
   * Return first/last/next/previous sibling of the accessible.
   */
  inline LocalAccessible* LocalNextSibling() const {
    return GetSiblingAtOffset(1);
  }
  inline LocalAccessible* LocalPrevSibling() const {
    return GetSiblingAtOffset(-1);
  }
  inline LocalAccessible* LocalFirstChild() const { return LocalChildAt(0); }
  inline LocalAccessible* LocalLastChild() const {
    uint32_t childCount = ChildCount();
    return childCount != 0 ? LocalChildAt(childCount - 1) : nullptr;
  }

  virtual uint32_t EmbeddedChildCount() override;

  /**
   * Return embedded accessible child at the given index.
   */
  virtual Accessible* EmbeddedChildAt(uint32_t aIndex) override;

  virtual int32_t IndexOfEmbeddedChild(Accessible* aChild) override;

  /**
   * Return number of content children/content child at index. The content
   * child is created from markup in contrast to it's never constructed by its
   * parent accessible (like treeitem accessibles for XUL trees).
   */
  uint32_t ContentChildCount() const { return mChildren.Length(); }
  LocalAccessible* ContentChildAt(uint32_t aIndex) const {
    return mChildren.ElementAt(aIndex);
  }

  /**
   * Return true if the accessible is attached to tree.
   */
  bool IsBoundToParent() const { return !!mParent; }

  //////////////////////////////////////////////////////////////////////////////
  // Miscellaneous methods

  /**
   * Handle accessible event, i.e. process it, notifies observers and fires
   * platform specific event.
   */
  virtual nsresult HandleAccEvent(AccEvent* aAccEvent);

  /**
   * Return true if the accessible is an acceptable child.
   */
  virtual bool IsAcceptableChild(nsIContent* aEl) const {
    return aEl &&
           !aEl->IsAnyOfHTMLElements(nsGkAtoms::option, nsGkAtoms::optgroup);
  }

  virtual void AppendTextTo(nsAString& aText, uint32_t aStartOffset = 0,
                            uint32_t aLength = UINT32_MAX) override;

  virtual nsRect BoundsInAppUnits() const override;

  virtual LayoutDeviceIntRect Bounds() const override;

  /**
   * Return boundaries rect relative the bounding frame.
   */
  virtual nsRect RelativeBounds(nsIFrame** aRelativeFrame) const;

  /**
   * Return boundaries rect relative to the frame of the parent accessible.
   * The returned bounds are the same regardless of whether the parent is
   * scrolled. This means the scroll position must be later subtracted to
   * calculate absolute coordinates.
   */
  virtual nsRect ParentRelativeBounds();

  /**
   * Selects the accessible within its container if applicable.
   */
  virtual void SetSelected(bool aSelect) override;

  /**
   * Select the accessible within its container.
   */
  virtual void TakeSelection() override;

  /**
   * Focus the accessible.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual void TakeFocus() const override;

  MOZ_CAN_RUN_SCRIPT
  virtual void ScrollTo(uint32_t aHow) const override;

  virtual void ScrollToPoint(uint32_t aCoordinateType, int32_t aX,
                             int32_t aY) override;

  /**
   * Get a pointer to accessibility interface for this node, which is specific
   * to the OS/accessibility toolkit we're running on.
   */
  virtual void GetNativeInterface(void** aNativeAccessible);

  virtual Maybe<int32_t> GetIntARIAAttr(nsAtom* aAttrName) const override;

  virtual bool GetStringARIAAttr(nsAtom* aAttrName,
                                 nsAString& aAttrValue) const override;

  //////////////////////////////////////////////////////////////////////////////
  // Downcasting and types

  inline bool IsAbbreviation() const {
    return mContent &&
           mContent->IsAnyOfHTMLElements(nsGkAtoms::abbr, nsGkAtoms::acronym);
  }

  ApplicationAccessible* AsApplication();

  DocAccessible* AsDoc();

  const DocAccessible* AsDoc() const {
    DocAccessible* doc = const_cast<LocalAccessible*>(this)->AsDoc();
    return const_cast<const DocAccessible*>(doc);
  }

  HyperTextAccessible* AsHyperText();
  virtual HyperTextAccessibleBase* AsHyperTextBase() override;

  HTMLLIAccessible* AsHTMLListItem();

  HTMLLinkAccessible* AsHTMLLink();

  ImageAccessible* AsImage();

  HTMLImageMapAccessible* AsImageMap();

  OuterDocAccessible* AsOuterDoc();

  a11y::RootAccessible* AsRoot();

  virtual TableAccessible* AsTable() override;
  virtual TableCellAccessible* AsTableCell() override;

  TextLeafAccessible* AsTextLeaf();

  XULLabelAccessible* AsXULLabel();

  XULTreeAccessible* AsXULTree();

  //////////////////////////////////////////////////////////////////////////////
  // ActionAccessible

  virtual bool HasPrimaryAction() const override;

  virtual uint8_t ActionCount() const override;

  virtual void ActionNameAt(uint8_t aIndex, nsAString& aName) override;

  virtual bool DoAction(uint8_t aIndex) const override;

  virtual KeyBinding AccessKey() const override;

  /**
   * Return global keyboard shortcut for default action, such as Ctrl+O for
   * Open file menuitem.
   */
  virtual KeyBinding KeyboardShortcut() const;

  //////////////////////////////////////////////////////////////////////////////
  // HyperLinkAccessible (any embedded object in text can implement HyperLink,
  // which helps determine where it is located within containing text).

  /**
   * Return true if the accessible is hyper link accessible.
   */
  virtual bool IsLink() const override;

  //////////////////////////////////////////////////////////////////////////////
  // SelectAccessible

  /**
   * Return an array of selected items.
   */
  virtual void SelectedItems(nsTArray<Accessible*>* aItems) override;

  /**
   * Return the number of selected items.
   */
  virtual uint32_t SelectedItemCount() override;

  /**
   * Return selected item at the given index.
   */
  virtual Accessible* GetSelectedItem(uint32_t aIndex) override;

  /**
   * Determine if item at the given index is selected.
   */
  virtual bool IsItemSelected(uint32_t aIndex) override;

  /**
   * Add item at the given index the selection. Return true if success.
   */
  virtual bool AddItemToSelection(uint32_t aIndex) override;

  /**
   * Remove item at the given index from the selection. Return if success.
   */
  virtual bool RemoveItemFromSelection(uint32_t aIndex) override;

  /**
   * Select all items. Return true if success.
   */
  virtual bool SelectAll() override;

  /**
   * Unselect all items. Return true if success.
   */
  virtual bool UnselectAll() override;

  //////////////////////////////////////////////////////////////////////////////
  // Value (numeric value interface)

  virtual double MaxValue() const override;
  virtual double MinValue() const override;
  virtual double CurValue() const override;
  virtual double Step() const override;
  virtual bool SetCurValue(double aValue) override;

  //////////////////////////////////////////////////////////////////////////////
  // Widgets

  /**
   * Return true if accessible is a widget, i.e. control or accessible that
   * manages its items. Note, being a widget the accessible may be a part of
   * composite widget.
   */
  virtual bool IsWidget() const;

  /**
   * Return true if the widget is active, i.e. has a focus within it.
   */
  virtual bool IsActiveWidget() const;

  /**
   * Return true if the widget has items and items are operable by user and
   * can be activated.
   */
  virtual bool AreItemsOperable() const;

  /**
   * Return the current item of the widget, i.e. an item that has or will have
   * keyboard focus when widget gets active.
   */
  virtual LocalAccessible* CurrentItem() const;

  /**
   * Set the current item of the widget.
   */
  virtual void SetCurrentItem(const LocalAccessible* aItem);

  /**
   * Return container widget this accessible belongs to.
   */
  virtual LocalAccessible* ContainerWidget() const;

  /**
   * Accessible's element ID is referenced as a aria-activedescendant in the
   * document. This method is only used for ID changes and therefore does not
   * need to work for direct element references via ariaActiveDescendantElement.
   */
  bool IsActiveDescendantId(LocalAccessible** aWidget = nullptr) const;

  /**
   * Return true if the accessible is defunct.
   */
  inline bool IsDefunct() const;

  /**
   * Return false if the accessible is no longer in the document.
   */
  bool IsInDocument() const { return !(mStateFlags & eIsNotInDocument); }

  /**
   * Return true if the accessible should be contained by document node map.
   */
  bool IsNodeMapEntry() const {
    return HasOwnContent() && !(mStateFlags & eNotNodeMapEntry);
  }

  /**
   * Return true if the accessible has associated DOM content.
   */
  bool HasOwnContent() const {
    return mContent && !(mStateFlags & eSharedNode);
  }

  /**
   * Return true if native markup has a numeric value.
   */
  inline bool NativeHasNumericValue() const;

  /**
   * Return true if ARIA specifies support for a numeric value.
   */
  inline bool ARIAHasNumericValue() const;

  /**
   * Return true if the accessible has a numeric value.
   */
  virtual bool HasNumericValue() const override;

  /**
   * Return true if the accessible state change is processed by handling proper
   * DOM UI event, if otherwise then false. For example, CheckboxAccessible
   * created for HTML:input@type="checkbox" will process
   * nsIDocumentObserver::ElementStateChanged instead of 'CheckboxStateChange'
   * event.
   */
  bool NeedsDOMUIEvent() const { return !(mStateFlags & eIgnoreDOMUIEvent); }

  /**
   * Get/set repositioned bit indicating that the accessible was moved in
   * the accessible tree, i.e. the accessible tree structure differs from DOM.
   */
  bool IsRelocated() const { return mStateFlags & eRelocated; }
  void SetRelocated(bool aRelocated) {
    if (aRelocated) {
      mStateFlags |= eRelocated;
    } else {
      mStateFlags &= ~eRelocated;
    }
  }

  /**
   * Return true if the accessible allows accessible children from subtree of
   * a DOM element of this accessible.
   */
  bool KidsFromDOM() const { return !(mStateFlags & eNoKidsFromDOM); }

  /**
   * Return true if this accessible has a parent, relation or ancestor with a
   * relation whose name depends on this accessible.
   */
  bool HasNameDependent() const { return mContextFlags & eHasNameDependent; }

  /**
   * Return true if this accessible has a parent, relation or ancestor with a
   * relation whose description depends on this accessible.
   */
  bool HasDescriptionDependent() const {
    return mContextFlags & eHasDescriptionDependent;
  }

  /**
   * Return true if the element is inside an alert.
   */
  bool IsInsideAlert() const { return mContextFlags & eInsideAlert; }

  /**
   * Return true if there is a pending reorder event for this accessible.
   */
  bool ReorderEventTarget() const { return mReorderEventTarget; }

  /**
   * Return true if there is a pending show event for this accessible.
   */
  bool ShowEventTarget() const { return mShowEventTarget; }

  /**
   * Return true if there is a pending hide event for this accessible.
   */
  bool HideEventTarget() const { return mHideEventTarget; }

  /**
   * Set if there is a pending reorder event for this accessible.
   */
  void SetReorderEventTarget(bool aTarget) { mReorderEventTarget = aTarget; }

  /**
   * Set if this accessible is a show event target.
   */
  void SetShowEventTarget(bool aTarget) { mShowEventTarget = aTarget; }

  /**
   * Set if this accessible is a hide event target.
   */
  void SetHideEventTarget(bool aTarget) { mHideEventTarget = aTarget; }

  void Announce(const nsAString& aAnnouncement, uint16_t aPriority);

  virtual bool IsRemote() const override { return false; }

  already_AddRefed<AccAttributes> BundleFieldsForCache(
      uint64_t aCacheDomain, CacheUpdateType aUpdateType,
      uint64_t aInitialDomains = CacheDomain::None);

  /**
   * Push fields to cache.
   * aCacheDomain - describes which fields to bundle and ultimately send
   * aUpdate - describes whether this is an initial or subsequent update
   * aAppendEventData - don't send the event now; append it to the mutation
   *                    events list on the DocAccessibleChild
   */
  void SendCache(uint64_t aCacheDomain, CacheUpdateType aUpdate,
                 bool aAppendEventData = false);

  void MaybeQueueCacheUpdateForStyleChanges();

  virtual nsAtom* TagName() const override;

  virtual already_AddRefed<nsAtom> DisplayStyle() const override;

  virtual float Opacity() const override;

  virtual void DOMNodeID(nsString& aID) const override;

  virtual void DOMNodeClass(nsString& aClass) const override;

  virtual void LiveRegionAttributes(nsAString* aLive, nsAString* aRelevant,
                                    Maybe<bool>* aAtomic,
                                    nsAString* aBusy) const override;

  virtual Maybe<bool> ARIASelected() const override;

 protected:
  virtual ~LocalAccessible();

  /**
   * Return the accessible name provided by native markup. It doesn't take
   * into account ARIA markup used to specify the name.
   */
  virtual mozilla::a11y::ENameValueFlag NativeName(nsString& aName) const;

  /**
   * Return the accessible description provided by native markup. It doesn't
   * take into account ARIA markup used to specify the description.
   */
  void NativeDescription(nsString& aDescription) const;

  uint64_t ExplicitState() const;

  /**
   * Return object attributes provided by native markup. It doesn't take into
   * account ARIA.
   */
  virtual already_AddRefed<AccAttributes> NativeAttributes();

  /**
   * The given attribute has the potential of changing the accessible's state.
   * This is used to capture the state before the attribute change and compare
   * it with the state after.
   */
  virtual bool AttributeChangesState(nsAtom* aAttribute);

  /**
   * Notify accessible that a DOM attribute on its associated content has
   * changed. This allows the accessible to update its state and emit any
   * relevant events.
   */
  virtual void DOMAttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                                   int32_t aModType,
                                   const nsAttrValue* aOldValue,
                                   uint64_t aOldState);

  //////////////////////////////////////////////////////////////////////////////
  // Initializing, cache and tree traverse methods

  /**
   * Destroy the object.
   */
  void LastRelease();

  /**
   * Set accessible parent and index in parent.
   */
  void BindToParent(LocalAccessible* aParent, uint32_t aIndexInParent);
  void UnbindFromParent();

  /**
   * Return sibling accessible at the given offset.
   */
  virtual LocalAccessible* GetSiblingAtOffset(int32_t aOffset,
                                              nsresult* aError = nullptr) const;

  void ModifySubtreeContextFlags(uint32_t aContextFlags, bool aAdd);

  /**
   * Flags used to describe the state of this accessible.
   */
  enum StateFlags {
    eIsDefunct = 1 << 0,        // accessible is defunct
    eIsNotInDocument = 1 << 1,  // accessible is not in document
    eSharedNode = 1 << 2,  // accessible shares DOM node from another accessible
    eNotNodeMapEntry = 1 << 3,   // accessible shouldn't be in document node map
    eGroupInfoDirty = 1 << 4,    // accessible needs to update group info
    eKidsMutating = 1 << 5,      // subtree is being mutated
    eIgnoreDOMUIEvent = 1 << 6,  // don't process DOM UI events for a11y events
    eRelocated = 1 << 7,         // accessible was moved in tree
    eNoKidsFromDOM = 1 << 8,     // accessible doesn't allow children from DOM
    eHasTextKids = 1 << 9,       // accessible have a text leaf in children
    eOldFrameHasValidTransformStyle =
        1 << 10,  // frame prior to most recent style change both has transform
                  // styling and supports transforms

    eLastStateFlag = eOldFrameHasValidTransformStyle
  };

  /**
   * Flags used for contextual information about the accessible.
   */
  enum ContextFlags {
    eHasNameDependent = 1 << 0,  // See HasNameDependent().
    eInsideAlert = 1 << 1,
    eHasDescriptionDependent = 1 << 2,  // See HasDescriptionDependent().

    eLastContextFlag = eHasDescriptionDependent
  };

 protected:
  //////////////////////////////////////////////////////////////////////////////
  // Miscellaneous helpers

  /**
   * Return ARIA role (helper method).
   */
  mozilla::a11y::role ARIATransformRole(mozilla::a11y::role aRole) const;

  /**
   * Return the minimum role that should be used as a last resort if the element
   * does not have a more specific role.
   */
  mozilla::a11y::role GetMinimumRole(mozilla::a11y::role aRole) const;

  //////////////////////////////////////////////////////////////////////////////
  // Name helpers

  /**
   * Returns the accessible name specified by ARIA.
   */
  ENameValueFlag ARIAName(nsString& aName) const;

  /**
   * Returns the accessible description specified by ARIA.
   */
  void ARIADescription(nsString& aDescription) const;

  /**
   * Returns the accessible name specified for this control using XUL
   * <label control="id" ...>.
   */
  static void NameFromAssociatedXULLabel(DocAccessible* aDocument,
                                         nsIContent* aElm, nsString& aName);

  /**
   * Return the name for XUL element.
   */
  static void XULElmName(DocAccessible* aDocument, nsIContent* aElm,
                         nsString& aName);

  // helper method to verify frames
  static nsresult GetFullKeyName(const nsAString& aModifierName,
                                 const nsAString& aKeyName,
                                 nsAString& aStringOut);

  //////////////////////////////////////////////////////////////////////////////
  // Action helpers

  /**
   * Prepares click action that will be invoked in timeout.
   *
   * @note  DoCommand() prepares an action in timeout because when action
   *  command opens a modal dialog/window, it won't return until the
   *  dialog/window is closed. If executing action command directly in
   *  nsIAccessible::DoAction() method, it will block AT tools (e.g. GOK) that
   *  invoke action of mozilla accessibles direclty (see bug 277888 for
   * details).
   *
   * @param  aActionIndex  [in, optional] index of accessible action
   */
  void DoCommand(uint32_t aActionIndex = 0) const;

  /**
   * Dispatch click event.
   */
  MOZ_CAN_RUN_SCRIPT
  virtual void DispatchClickEvent(uint32_t aActionIndex) const;

  //////////////////////////////////////////////////////////////////////////////
  // Helpers

  /**
   *  Get the container node for an atomic region, defined by aria-atomic="true"
   *  @return the container node
   */
  nsIContent* GetAtomicRegion() const;

  /**
   * Return numeric value of the given ARIA attribute, NaN if not applicable.
   *
   * @param aARIAProperty  [in] the ARIA property we're using
   * @return  a numeric value
   */
  double AttrNumericValue(nsAtom* aARIAAttr) const;

  /**
   * Return the action rule based on ARIA enum constants EActionRule
   * (see ARIAMap.h). Used by ActionCount() and ActionNameAt().
   */
  uint32_t GetActionRule() const;

  virtual AccGroupInfo* GetGroupInfo() const override;

  virtual AccGroupInfo* GetOrCreateGroupInfo() override;

  virtual void ARIAGroupPosition(int32_t* aLevel, int32_t* aSetSize,
                                 int32_t* aPosInSet) const override;

  // Data Members
  // mContent can be null in a DocAccessible if the document has no body or
  // root element, or if the initial tree hasn't been constructed yet.
  nsCOMPtr<nsIContent> mContent;
  RefPtr<DocAccessible> mDoc;

  LocalAccessible* mParent;
  nsTArray<LocalAccessible*> mChildren;
  int32_t mIndexInParent;

  // These are used to determine whether to send cache updates.
  Maybe<nsRect> mBounds;
  int32_t mFirstLineStart;

  /**
   * Maintain a reference to the ComputedStyle of our frame so we can
   * send cache updates when style changes are observed.
   *
   * This RefPtr is initialised in BundleFieldsForCache to the ComputedStyle
   * for our initial frame.
   * Style changes are observed in one of two ways:
   * 1. Style changes on the same frame are observed in
   * nsIFrame::DidSetComputedStyle.
   * 2. Style changes for reconstructed frames are handled in
   * DocAccessible::PruneOrInsertSubtree.
   * In both cases, we call into MaybeQueueCacheUpdateForStyleChanges. There, we
   * compare a11y-relevant properties in mOldComputedStyle with the current
   * ComputedStyle fetched from GetFrame()->Style(). Finally, we send cache
   * updates for attributes affected by the style change and update
   * mOldComputedStyle to the style of our current frame.
   */
  RefPtr<const ComputedStyle> mOldComputedStyle;

  static const uint8_t kStateFlagsBits = 11;
  static const uint8_t kContextFlagsBits = 3;

  /**
   * Keep in sync with StateFlags, ContextFlags, and AccTypes.
   */
  mutable uint32_t mStateFlags : kStateFlagsBits;
  uint32_t mContextFlags : kContextFlagsBits;
  uint32_t mReorderEventTarget : 1;
  uint32_t mShowEventTarget : 1;
  uint32_t mHideEventTarget : 1;

  void StaticAsserts() const;

#ifdef A11Y_LOG
  friend void logging::Tree(const char* aTitle, const char* aMsgText,
                            LocalAccessible* aRoot,
                            logging::GetTreePrefix aPrefixFunc,
                            void* aGetTreePrefixData);
  friend void logging::TreeSize(const char* aTitle, const char* aMsgText,
                                LocalAccessible* aRoot);
#endif
  friend class DocAccessible;
  friend class xpcAccessible;
  friend class TreeMutation;

  UniquePtr<mozilla::a11y::EmbeddedObjCollector> mEmbeddedObjCollector;
  int32_t mIndexOfEmbeddedChild;

  friend class EmbeddedObjCollector;

  mutable AccGroupInfo* mGroupInfo;
  friend class AccGroupInfo;

 private:
  LocalAccessible() = delete;
  LocalAccessible(const LocalAccessible&) = delete;
  LocalAccessible& operator=(const LocalAccessible&) = delete;

  /**
   * Traverses the accessible's parent chain in search of an accessible with
   * a frame. Returns the frame when found. Includes special handling for
   * OOP iframe docs and tab documents.
   */
  nsIFrame* FindNearestAccessibleAncestorFrame();

  /*
   * This function assumes that the current role is not valid. It searches for a
   * fallback role in the role attribute string, and returns it. If there is no
   * valid fallback role in the role attribute string, the function returns the
   * native role. The aRolesToSkip parameter will cause the function to skip any
   * roles found in the role attribute string when searching for the next valid
   * role.
   */
  role FindNextValidARIARole(
      std::initializer_list<nsStaticAtom*> aRolesToSkip) const;

  LocalAccessible* GetPopoverTargetDetailsRelation() const;
};

////////////////////////////////////////////////////////////////////////////////
// LocalAccessible downcasting method

inline LocalAccessible* Accessible::AsLocal() {
  return IsLocal() ? static_cast<LocalAccessible*>(this) : nullptr;
}

}  // namespace a11y
}  // namespace mozilla

#endif
