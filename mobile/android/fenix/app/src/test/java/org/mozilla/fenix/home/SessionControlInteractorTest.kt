/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import mozilla.components.feature.tab.collections.Tab
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.service.pocket.PocketStory
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.bookmarks.controller.BookmarksController
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesCategory
import org.mozilla.fenix.home.pocket.controller.PocketStoriesController
import org.mozilla.fenix.home.privatebrowsing.controller.PrivateBrowsingController
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTab
import org.mozilla.fenix.home.recentsyncedtabs.controller.RecentSyncedTabController
import org.mozilla.fenix.home.recenttabs.controller.RecentTabController
import org.mozilla.fenix.home.recentvisits.controller.RecentVisitsController
import org.mozilla.fenix.home.search.HomeSearchController
import org.mozilla.fenix.home.sessioncontrol.DefaultSessionControlController
import org.mozilla.fenix.home.sessioncontrol.SessionControlInteractor
import org.mozilla.fenix.home.toolbar.ToolbarController
import org.mozilla.fenix.search.toolbar.SearchSelectorController

class SessionControlInteractorTest {

    private val controller: DefaultSessionControlController = mockk(relaxed = true)
    private val recentTabController: RecentTabController = mockk(relaxed = true)
    private val recentSyncedTabController: RecentSyncedTabController = mockk(relaxed = true)
    private val bookmarksController: BookmarksController = mockk(relaxed = true)
    private val pocketStoriesController: PocketStoriesController = mockk(relaxed = true)
    private val privateBrowsingController: PrivateBrowsingController = mockk(relaxed = true)
    private val searchSelectorController: SearchSelectorController = mockk(relaxed = true)
    private val toolbarController: ToolbarController = mockk(relaxed = true)
    private val homeSearchController: HomeSearchController = mockk(relaxed = true)

    // Note: the recent visits tests are handled in [RecentVisitsInteractorTest] and [RecentVisitsControllerTest]
    private val recentVisitsController: RecentVisitsController = mockk(relaxed = true)

    private lateinit var interactor: SessionControlInteractor

    @Before
    fun setup() {
        interactor = SessionControlInteractor(
            controller,
            recentTabController,
            recentSyncedTabController,
            bookmarksController,
            recentVisitsController,
            pocketStoriesController,
            privateBrowsingController,
            searchSelectorController,
            toolbarController,
            homeSearchController,
        )
    }

    @Test
    fun onCollectionAddTabTapped() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onCollectionAddTabTapped(collection)
        verify { controller.handleCollectionAddTabTapped(collection) }
    }

    @Test
    fun onCollectionOpenTabClicked() {
        val tab: Tab = mockk(relaxed = true)
        interactor.onCollectionOpenTabClicked(tab)
        verify { controller.handleCollectionOpenTabClicked(tab) }
    }

    @Test
    fun onCollectionOpenTabsTapped() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onCollectionOpenTabsTapped(collection)
        verify { controller.handleCollectionOpenTabsTapped(collection) }
    }

    @Test
    fun onCollectionRemoveTab() {
        val collection: TabCollection = mockk(relaxed = true)
        val tab: Tab = mockk(relaxed = true)
        interactor.onCollectionRemoveTab(collection, tab)
        verify { controller.handleCollectionRemoveTab(collection, tab) }
    }

    @Test
    fun onCollectionShareTabsClicked() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onCollectionShareTabsClicked(collection)
        verify { controller.handleCollectionShareTabsClicked(collection) }
    }

    @Test
    fun onDeleteCollectionTapped() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onDeleteCollectionTapped(collection)
        verify { controller.handleDeleteCollectionTapped(collection) }
    }

    @Test
    fun onPrivateBrowsingLearnMoreClicked() {
        interactor.onLearnMoreClicked()
        verify { privateBrowsingController.handleLearnMoreClicked() }
    }

    @Test
    fun onRenameCollectionTapped() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onRenameCollectionTapped(collection)
        verify { controller.handleRenameCollectionTapped(collection) }
    }

    @Test
    fun onToggleCollectionExpanded() {
        val collection: TabCollection = mockk(relaxed = true)
        interactor.onToggleCollectionExpanded(collection, true)
        verify { controller.handleToggleCollectionExpanded(collection, true) }
    }

    @Test
    fun onAddTabsToCollection() {
        interactor.onAddTabsToCollectionTapped()
        verify { controller.handleCreateCollection() }
    }

    @Test
    fun onPaste() {
        interactor.onPaste("text")
        verify { toolbarController.handlePaste("text") }
    }

    @Test
    fun onPasteAndGo() {
        interactor.onPasteAndGo("text")
        verify { toolbarController.handlePasteAndGo("text") }
    }

    @Test
    fun onNavigateSearch() {
        interactor.onNavigateSearch()
        verify { toolbarController.handleNavigateSearch() }
    }

    @Test
    fun onHomeContentFocusedWhileSearchIsActive() {
        interactor.onHomeContentFocusedWhileSearchIsActive()
        verify { homeSearchController.handleHomeContentFocusedWhileSearchIsActive() }
    }

    @Test
    fun onRemoveCollectionsPlaceholder() {
        interactor.onRemoveCollectionsPlaceholder()
        verify { controller.handleRemoveCollectionsPlaceholder() }
    }

    @Test
    fun onRecentTabClicked() {
        val tabId = "tabId"
        interactor.onRecentTabClicked(tabId)
        verify { recentTabController.handleRecentTabClicked(tabId) }
    }

    @Test
    fun onRecentTabShowAllClicked() {
        interactor.onRecentTabShowAllClicked()
        verify { recentTabController.handleRecentTabShowAllClicked() }
    }

    @Test
    fun `WHEN recent synced tab is clicked THEN the tab is handled`() {
        val tab: RecentSyncedTab = mockk()
        interactor.onRecentSyncedTabClicked(tab)

        verify { recentSyncedTabController.handleRecentSyncedTabClick(tab) }
    }

    @Test
    fun `WHEN recent synced tabs show all is clicked THEN show all synced tabs is handled`() {
        interactor.onSyncedTabShowAllClicked()

        verify { recentSyncedTabController.handleSyncedTabShowAllClicked() }
    }

    @Test
    fun `WHEN a bookmark is clicked THEN the selected bookmark is handled`() {
        val bookmark = Bookmark()

        interactor.onBookmarkClicked(bookmark)
        verify { bookmarksController.handleBookmarkClicked(bookmark) }
    }

    @Test
    fun `WHEN Show All bookmarks button is clicked THEN the click is handled`() {
        interactor.onShowAllBookmarksClicked()
        verify { bookmarksController.handleShowAllBookmarksClicked() }
    }

    @Test
    fun `WHEN private mode button is clicked THEN the click is handled`() {
        val newMode = BrowsingMode.Private

        interactor.onPrivateModeButtonClicked(newMode)
        verify { privateBrowsingController.handlePrivateModeButtonClicked(newMode) }
    }

    @Test
    fun `WHEN onSettingsClicked is called THEN handleTopSiteSettingsClicked is called`() {
        interactor.onSettingsClicked()
        verify { controller.handleTopSiteSettingsClicked() }
    }

    @Test
    fun `WHEN onSponsorPrivacyClicked is called THEN handleSponsorPrivacyClicked is called`() {
        interactor.onSponsorPrivacyClicked()
        verify { controller.handleSponsorPrivacyClicked() }
    }

    @Test
    fun `WHEN a top site is long clicked THEN the click is handled`() {
        val topSite: TopSite = mockk()
        interactor.onTopSiteLongClicked(topSite)
        verify { controller.handleTopSiteLongClicked(topSite) }
    }

    @Test
    fun `GIVEN a PocketStoriesInteractor WHEN a story is shown THEN handle it in a PocketStoriesController`() {
        val shownStory: PocketStory = mockk()
        val storyPosition = Triple(1, 2, 3)

        interactor.onStoryShown(shownStory, storyPosition)

        verify { pocketStoriesController.handleStoryShown(shownStory, storyPosition) }
    }

    @Test
    fun `GIVEN a PocketStoriesInteractor WHEN stories are shown THEN handle it in a PocketStoriesController`() {
        val shownStories: List<PocketStory> = mockk()

        interactor.onStoriesShown(shownStories)

        verify { pocketStoriesController.handleStoriesShown(shownStories) }
    }

    @Test
    fun `GIVEN a PocketStoriesInteractor WHEN a category is clicked THEN handle it in a PocketStoriesController`() {
        val clickedCategory: PocketRecommendedStoriesCategory = mockk()

        interactor.onCategoryClicked(clickedCategory)

        verify { pocketStoriesController.handleCategoryClick(clickedCategory) }
    }

    @Test
    fun `GIVEN a PocketStoriesInteractor WHEN a story is clicked THEN handle it in a PocketStoriesController`() {
        val clickedStory: PocketStory = mockk()
        val storyPosition = Triple(1, 2, 3)

        interactor.onStoryClicked(clickedStory, storyPosition)

        verify { pocketStoriesController.handleStoryClicked(clickedStory, storyPosition) }
    }

    @Test
    fun reportSessionMetrics() {
        val appState: AppState = mockk(relaxed = true)
        every { appState.bookmarks } returns emptyList()
        interactor.reportSessionMetrics(appState)
        verify { controller.handleReportSessionMetrics(appState) }
    }
}
