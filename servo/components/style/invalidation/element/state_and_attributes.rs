/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! An invalidation processor for style changes due to state and attribute
//! changes.

use crate::context::SharedStyleContext;
use crate::data::ElementData;
use crate::dom::{TElement, TNode};
use crate::invalidation::element::element_wrapper::{ElementSnapshot, ElementWrapper};
use crate::invalidation::element::invalidation_map::*;
use crate::invalidation::element::invalidator::{
    DescendantInvalidationLists, InvalidationVector, SiblingTraversalMap,
};
use crate::invalidation::element::invalidator::{Invalidation, InvalidationProcessor};
use crate::invalidation::element::restyle_hints::RestyleHint;
use crate::selector_map::SelectorMap;
use crate::selector_parser::Snapshot;
use crate::stylesheets::origin::OriginSet;
use crate::values::AtomIdent;
use crate::{Atom, WeakAtom};
use dom::ElementState;
use selectors::attr::CaseSensitivity;
use selectors::kleene_value::KleeneValue;
use selectors::matching::{
    matches_selector_kleene, IncludeStartingStyle, MatchingContext, MatchingForInvalidation,
    MatchingMode, NeedsSelectorFlags, SelectorCaches, VisitedHandlingMode,
};
use smallvec::SmallVec;

/// The collector implementation.
struct Collector<'a, 'b: 'a, 'selectors: 'a, E>
where
    E: TElement,
{
    element: E,
    wrapper: ElementWrapper<'b, E>,
    snapshot: &'a Snapshot,
    matching_context: &'a mut MatchingContext<'b, E::Impl>,
    lookup_element: E,
    removed_id: Option<&'a WeakAtom>,
    added_id: Option<&'a WeakAtom>,
    classes_removed: &'a SmallVec<[Atom; 8]>,
    classes_added: &'a SmallVec<[Atom; 8]>,
    custom_states_removed: &'a SmallVec<[AtomIdent; 8]>,
    custom_states_added: &'a SmallVec<[AtomIdent; 8]>,
    state_changes: ElementState,
    descendant_invalidations: &'a mut DescendantInvalidationLists<'selectors>,
    sibling_invalidations: &'a mut InvalidationVector<'selectors>,
    invalidates_self: bool,
}

/// An invalidation processor for style changes due to state and attribute
/// changes.
pub struct StateAndAttrInvalidationProcessor<'a, 'b: 'a, E: TElement> {
    shared_context: &'a SharedStyleContext<'b>,
    element: E,
    data: &'a mut ElementData,
    matching_context: MatchingContext<'a, E::Impl>,
    traversal_map: SiblingTraversalMap<E>,
}

impl<'a, 'b: 'a, E: TElement + 'b> StateAndAttrInvalidationProcessor<'a, 'b, E> {
    /// Creates a new StateAndAttrInvalidationProcessor.
    pub fn new(
        shared_context: &'a SharedStyleContext<'b>,
        element: E,
        data: &'a mut ElementData,
        selector_caches: &'a mut SelectorCaches,
    ) -> Self {
        let matching_context = MatchingContext::new_for_visited(
            MatchingMode::Normal,
            None,
            selector_caches,
            VisitedHandlingMode::AllLinksVisitedAndUnvisited,
            IncludeStartingStyle::No,
            shared_context.quirks_mode(),
            NeedsSelectorFlags::No,
            MatchingForInvalidation::Yes,
        );

        Self {
            shared_context,
            element,
            data,
            matching_context,
            traversal_map: SiblingTraversalMap::default(),
        }
    }
}

/// Checks a dependency against a given element and wrapper, to see if something
/// changed.
pub fn check_dependency<E, W>(
    dependency: &Dependency,
    element: &E,
    wrapper: &W,
    context: &mut MatchingContext<'_, E::Impl>,
) -> bool
where
    E: TElement,
    W: selectors::Element<Impl = E::Impl>,
{
    context.for_invalidation_comparison(|context| {
        let matches_now = matches_selector_kleene(
            &dependency.selector,
            dependency.selector_offset,
            None,
            element,
            context,
        );

        let matched_then = matches_selector_kleene(
            &dependency.selector,
            dependency.selector_offset,
            None,
            wrapper,
            context,
        );

        matched_then != matches_now || matches_now == KleeneValue::Unknown
    })
}

/// Whether we should process the descendants of a given element for style
/// invalidation.
pub fn should_process_descendants(data: &ElementData) -> bool {
    !data.styles.is_display_none() && !data.hint.contains(RestyleHint::RESTYLE_DESCENDANTS)
}

/// Propagates the bits after invalidating a descendant child.
pub fn propagate_dirty_bit_up_to<E>(ancestor: E, child: E)
where
    E: TElement,
{
    // The child may not be a flattened tree child of the current element,
    // but may be arbitrarily deep.
    //
    // Since we keep the traversal flags in terms of the flattened tree,
    // we need to propagate it as appropriate.
    let mut current = child.traversal_parent();
    while let Some(parent) = current.take() {
        unsafe { parent.set_dirty_descendants() };
        current = parent.traversal_parent();

        if parent == ancestor {
            return;
        }
    }
    debug_assert!(
        false,
        "Should've found {:?} as an ancestor of {:?}",
        ancestor, child
    );
}

/// Propagates the bits after invalidating a descendant child, if needed.
pub fn invalidated_descendants<E>(element: E, child: E)
where
    E: TElement,
{
    if !child.has_data() {
        return;
    }
    propagate_dirty_bit_up_to(element, child)
}

/// Sets the appropriate restyle hint after invalidating the style of a given
/// element.
pub fn invalidated_self<E>(element: E) -> bool
where
    E: TElement,
{
    let mut data = match element.mutate_data() {
        Some(data) => data,
        None => return false,
    };
    data.hint.insert(RestyleHint::RESTYLE_SELF);
    true
}

/// Sets the appropriate hint after invalidating the style of a sibling.
pub fn invalidated_sibling<E>(element: E, of: E)
where
    E: TElement,
{
    debug_assert_eq!(
        element.as_node().parent_node(),
        of.as_node().parent_node(),
        "Should be siblings"
    );
    if !invalidated_self(element) {
        return;
    }
    if element.traversal_parent() != of.traversal_parent() {
        let parent = element.as_node().parent_element_or_host();
        debug_assert!(
            parent.is_some(),
            "How can we have siblings without parent nodes?"
        );
        if let Some(e) = parent {
            propagate_dirty_bit_up_to(e, element)
        }
    }
}

impl<'a, 'b: 'a, E: 'a> InvalidationProcessor<'a, 'a, E>
    for StateAndAttrInvalidationProcessor<'a, 'b, E>
where
    E: TElement,
{
    /// We need to invalidate style on pseudo-elements, in order to process
    /// changes that could otherwise end up in ::before or ::after content being
    /// generated, and invalidate lazy pseudo caches.
    fn invalidates_on_pseudo_element(&self) -> bool {
        true
    }

    fn check_outer_dependency(&mut self, dependency: &Dependency, element: E) -> bool {
        // We cannot assert about `element` having a snapshot here (in fact it
        // most likely won't), because it may be an arbitrary descendant or
        // later-sibling of the element we started invalidating with.
        let wrapper = ElementWrapper::new(element, &*self.shared_context.snapshot_map);
        check_dependency(dependency, &element, &wrapper, &mut self.matching_context)
    }

    fn matching_context(&mut self) -> &mut MatchingContext<'a, E::Impl> {
        &mut self.matching_context
    }

    fn sibling_traversal_map(&self) -> &SiblingTraversalMap<E> {
        &self.traversal_map
    }

    fn collect_invalidations(
        &mut self,
        element: E,
        _self_invalidations: &mut InvalidationVector<'a>,
        descendant_invalidations: &mut DescendantInvalidationLists<'a>,
        sibling_invalidations: &mut InvalidationVector<'a>,
    ) -> bool {
        debug_assert_eq!(element, self.element);
        debug_assert!(element.has_snapshot(), "Why bothering?");

        let wrapper = ElementWrapper::new(element, &*self.shared_context.snapshot_map);

        let state_changes = wrapper.state_changes();
        let Some(snapshot) = wrapper.snapshot() else {
            return false;
        };

        if !snapshot.has_attrs() && !snapshot.has_custom_states() && state_changes.is_empty() {
            return false;
        }

        let mut classes_removed = SmallVec::<[Atom; 8]>::new();
        let mut classes_added = SmallVec::<[Atom; 8]>::new();
        if snapshot.class_changed() {
            // TODO(emilio): Do this more efficiently!
            snapshot.each_class(|c| {
                if !element.has_class(c, CaseSensitivity::CaseSensitive) {
                    classes_removed.push(c.0.clone())
                }
            });

            element.each_class(|c| {
                if !snapshot.has_class(c, CaseSensitivity::CaseSensitive) {
                    classes_added.push(c.0.clone())
                }
            })
        }

        let mut custom_states_removed = SmallVec::<[AtomIdent; 8]>::new();
        let mut custom_states_added = SmallVec::<[AtomIdent; 8]>::new();
        if snapshot.has_custom_states() {
            snapshot.each_custom_state(|s| {
                if !element.has_custom_state(s) {
                    custom_states_removed.push(s.clone())
                }
            });
            element.each_custom_state(|s| {
                if !snapshot.has_custom_state(s) {
                    custom_states_added.push(s.clone())
                }
            })
        }

        let mut id_removed = None;
        let mut id_added = None;
        if snapshot.id_changed() {
            let old_id = snapshot.id_attr();
            let current_id = element.id();

            if old_id != current_id {
                id_removed = old_id;
                id_added = current_id;
            }
        }

        if log_enabled!(::log::Level::Debug) {
            debug!("Collecting changes for: {:?}", element);
            if !state_changes.is_empty() {
                debug!(" > state: {:?}", state_changes);
            }
            if snapshot.id_changed() {
                debug!(" > id changed: +{:?} -{:?}", id_added, id_removed);
            }
            if snapshot.class_changed() {
                debug!(
                    " > class changed: +{:?} -{:?}",
                    classes_added, classes_removed
                );
            }
            let mut attributes_changed = false;
            snapshot.each_attr_changed(|_| {
                attributes_changed = true;
            });
            if attributes_changed {
                debug!(
                    " > attributes changed, old: {}",
                    snapshot.debug_list_attributes()
                )
            }
        }

        let lookup_element = if element.implemented_pseudo_element().is_some() {
            element.pseudo_element_originating_element().unwrap()
        } else {
            element
        };

        let mut shadow_rule_datas = SmallVec::<[_; 3]>::new();
        let matches_document_author_rules =
            element.each_applicable_non_document_style_rule_data(|data, host| {
                shadow_rule_datas.push((data, host.opaque()))
            });

        let invalidated_self = {
            let mut collector = Collector {
                wrapper,
                lookup_element,
                state_changes,
                element,
                snapshot: &snapshot,
                matching_context: &mut self.matching_context,
                removed_id: id_removed,
                added_id: id_added,
                classes_removed: &classes_removed,
                classes_added: &classes_added,
                custom_states_removed: &custom_states_removed,
                custom_states_added: &custom_states_added,
                descendant_invalidations,
                sibling_invalidations,
                invalidates_self: false,
            };

            let document_origins = if !matches_document_author_rules {
                OriginSet::ORIGIN_USER_AGENT | OriginSet::ORIGIN_USER
            } else {
                OriginSet::all()
            };

            for (cascade_data, origin) in self.shared_context.stylist.iter_origins() {
                if document_origins.contains(origin.into()) {
                    collector
                        .collect_dependencies_in_invalidation_map(cascade_data.invalidation_map());
                }
            }

            for &(ref data, ref host) in &shadow_rule_datas {
                collector.matching_context.current_host = Some(host.clone());
                collector.collect_dependencies_in_invalidation_map(data.invalidation_map());
            }

            collector.invalidates_self
        };

        // If we generated a ton of descendant invalidations, it's probably not
        // worth to go ahead and try to process them.
        //
        // Just restyle the descendants directly.
        //
        // This number is completely made-up, but the page that made us add this
        // code generated 1960+ invalidations (bug 1420741).
        //
        // We don't look at slotted_descendants because those don't propagate
        // down more than one level anyway.
        if descendant_invalidations.dom_descendants.len() > 150 {
            self.data.hint.insert(RestyleHint::RESTYLE_DESCENDANTS);
        }

        if invalidated_self {
            self.data.hint.insert(RestyleHint::RESTYLE_SELF);
        }

        invalidated_self
    }

    fn should_process_descendants(&mut self, element: E) -> bool {
        if element == self.element {
            return should_process_descendants(&self.data);
        }

        match element.borrow_data() {
            Some(d) => should_process_descendants(&d),
            None => return false,
        }
    }

    fn recursion_limit_exceeded(&mut self, element: E) {
        if element == self.element {
            self.data.hint.insert(RestyleHint::RESTYLE_DESCENDANTS);
            return;
        }

        if let Some(mut data) = element.mutate_data() {
            data.hint.insert(RestyleHint::RESTYLE_DESCENDANTS);
        }
    }

    fn invalidated_descendants(&mut self, element: E, child: E) {
        invalidated_descendants(element, child)
    }

    fn invalidated_self(&mut self, element: E) {
        debug_assert_ne!(element, self.element);
        invalidated_self(element);
    }

    fn invalidated_sibling(&mut self, element: E, of: E) {
        debug_assert_ne!(element, self.element);
        invalidated_sibling(element, of);
    }
}

impl<'a, 'b, 'selectors, E> Collector<'a, 'b, 'selectors, E>
where
    E: TElement,
    'selectors: 'a,
{
    fn collect_dependencies_in_invalidation_map(&mut self, map: &'selectors InvalidationMap) {
        let quirks_mode = self.matching_context.quirks_mode();
        let removed_id = self.removed_id;
        if let Some(ref id) = removed_id {
            if let Some(deps) = map.id_to_selector.get(id, quirks_mode) {
                for dep in deps {
                    self.scan_dependency(dep);
                }
            }
        }

        let added_id = self.added_id;
        if let Some(ref id) = added_id {
            if let Some(deps) = map.id_to_selector.get(id, quirks_mode) {
                for dep in deps {
                    self.scan_dependency(dep);
                }
            }
        }

        for class in self.classes_added.iter().chain(self.classes_removed.iter()) {
            if let Some(deps) = map.class_to_selector.get(class, quirks_mode) {
                for dep in deps {
                    self.scan_dependency(dep);
                }
            }
        }

        for state in self
            .custom_states_added
            .iter()
            .chain(self.custom_states_removed.iter())
        {
            if let Some(deps) = map.custom_state_affecting_selectors.get(state) {
                for dep in deps {
                    self.scan_dependency(dep);
                }
            }
        }

        self.snapshot.each_attr_changed(|attribute| {
            if let Some(deps) = map.other_attribute_affecting_selectors.get(attribute) {
                for dep in deps {
                    self.scan_dependency(dep);
                }
            }
        });

        self.collect_state_dependencies(&map.state_affecting_selectors)
    }

    fn collect_state_dependencies(&mut self, map: &'selectors SelectorMap<StateDependency>) {
        if self.state_changes.is_empty() {
            return;
        }
        map.lookup_with_additional(
            self.lookup_element,
            self.matching_context.quirks_mode(),
            self.removed_id,
            self.classes_removed,
            self.state_changes,
            |dependency| {
                if !dependency.state.intersects(self.state_changes) {
                    return true;
                }
                self.scan_dependency(&dependency.dep);
                true
            },
        );
    }

    /// Check whether a dependency should be taken into account.
    #[inline]
    fn check_dependency(&mut self, dependency: &Dependency) -> bool {
        check_dependency(
            dependency,
            &self.element,
            &self.wrapper,
            &mut self.matching_context,
        )
    }

    fn scan_dependency(&mut self, dependency: &'selectors Dependency) {
        debug_assert!(
            matches!(
                dependency.invalidation_kind(),
                DependencyInvalidationKind::Normal(_)
            ),
            "Found relative selector dependency"
        );
        debug!(
            "TreeStyleInvalidator::scan_dependency({:?}, {:?})",
            self.element, dependency
        );

        if !self.dependency_may_be_relevant(dependency) {
            return;
        }

        if self.check_dependency(dependency) {
            return self.note_dependency(dependency);
        }
    }

    fn note_dependency(&mut self, dependency: &'selectors Dependency) {
        debug_assert!(self.dependency_may_be_relevant(dependency));

        let invalidation_kind = dependency.normal_invalidation_kind();
        if matches!(invalidation_kind, NormalDependencyInvalidationKind::Element) {
            if let Some(ref next) = dependency.next {
                // We know something changed in the inner selector, go outwards
                // now.
                self.scan_dependency(&next.as_ref().slice()[0]);
            } else {
                self.invalidates_self = true;
            }
            return;
        }

        debug_assert_ne!(dependency.selector_offset, 0);
        debug_assert_ne!(dependency.selector_offset, dependency.selector.len());

        let invalidation =
            Invalidation::new(&dependency, self.matching_context.current_host.clone());

        self.invalidates_self |= push_invalidation(
            invalidation,
            invalidation_kind,
            self.descendant_invalidations,
            self.sibling_invalidations,
        );
    }

    /// Returns whether `dependency` may cause us to invalidate the style of
    /// more elements than what we've already invalidated.
    fn dependency_may_be_relevant(&self, dependency: &Dependency) -> bool {
        match dependency.normal_invalidation_kind() {
            NormalDependencyInvalidationKind::Element => !self.invalidates_self,
            NormalDependencyInvalidationKind::SlottedElements => {
                self.element.is_html_slot_element()
            },
            NormalDependencyInvalidationKind::Parts => self.element.shadow_root().is_some(),
            NormalDependencyInvalidationKind::ElementAndDescendants |
            NormalDependencyInvalidationKind::Siblings |
            NormalDependencyInvalidationKind::Descendants => true,
        }
    }
}

pub(crate) fn push_invalidation<'a>(
    invalidation: Invalidation<'a>,
    invalidation_kind: NormalDependencyInvalidationKind,
    descendant_invalidations: &mut DescendantInvalidationLists<'a>,
    sibling_invalidations: &mut InvalidationVector<'a>,
) -> bool {
    match invalidation_kind {
        NormalDependencyInvalidationKind::Element => unreachable!(),
        NormalDependencyInvalidationKind::ElementAndDescendants => {
            descendant_invalidations.dom_descendants.push(invalidation);
            true
        },
        NormalDependencyInvalidationKind::Descendants => {
            descendant_invalidations.dom_descendants.push(invalidation);
            false
        },
        NormalDependencyInvalidationKind::Siblings => {
            sibling_invalidations.push(invalidation);
            false
        },
        NormalDependencyInvalidationKind::Parts => {
            descendant_invalidations.parts.push(invalidation);
            false
        },
        NormalDependencyInvalidationKind::SlottedElements => {
            descendant_invalidations
                .slotted_descendants
                .push(invalidation);
            false
        },
    }
}

pub(crate) fn dependency_may_be_relevant<E: TElement>(
    dependency: &Dependency,
    element: &E,
    already_invalidated_self: bool,
) -> bool {
    match dependency.normal_invalidation_kind() {
        NormalDependencyInvalidationKind::Element => !already_invalidated_self,
        NormalDependencyInvalidationKind::SlottedElements => element.is_html_slot_element(),
        NormalDependencyInvalidationKind::Parts => element.shadow_root().is_some(),
        NormalDependencyInvalidationKind::ElementAndDescendants |
        NormalDependencyInvalidationKind::Siblings |
        NormalDependencyInvalidationKind::Descendants => true,
    }
}
