/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.LinkText
import org.mozilla.fenix.compose.LinkTextState
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE_PRIVATE_BROWSING_LEARN_MORE_LINK
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Total Private Browsing Mode homepage informational card.
 *
 * @param onLearnMoreClick Invoked when the user clicks on the who can see my activity link.
 */
@OptIn(ExperimentalComposeUiApi::class)
@Composable
fun PrivateBrowsingDescription(
    onLearnMoreClick: () -> Unit,
) {
    Box(
        modifier = Modifier
            .shadow(elevation = 5.dp, shape = RoundedCornerShape(8.dp), clip = true)
            .clip(shape = RoundedCornerShape(8.dp))
            .fillMaxWidth()
            .wrapContentHeight()
            .background(FirefoxTheme.colors.layer2),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
        ) {
            Text(
                text = stringResource(id = R.string.felt_privacy_desc_card_title),
                style = FirefoxTheme.typography.headline7,
                color = FirefoxTheme.colors.textPrimary,
            )

            Spacer(modifier = Modifier.height(8.dp))

            Box(
                modifier = Modifier.semantics {
                    testTagsAsResourceId = true
                    testTag = HOMEPAGE_PRIVATE_BROWSING_LEARN_MORE_LINK
                },
            ) {
                LinkText(
                    text = stringResource(
                        id = R.string.felt_privacy_info_card_subtitle_2,
                        stringResource(id = R.string.app_name),
                        stringResource(id = R.string.felt_privacy_info_card_subtitle_link_text),
                    ),
                    linkTextStates = listOf(
                        LinkTextState(
                            text = stringResource(id = R.string.felt_privacy_info_card_subtitle_link_text),
                            url = "",
                            onClick = { onLearnMoreClick() },
                        ),
                    ),
                    style = FirefoxTheme.typography.body2.copy(
                        color = FirefoxTheme.colors.textPrimary,
                    ),
                    linkTextColor = FirefoxTheme.colors.textPrimary,
                    linkTextDecoration = TextDecoration.Underline,
                )
            }
        }
    }
}

@Composable
@Preview
private fun FeltPrivacyModeDescriptionPreview() {
    FirefoxTheme {
        Column(
            modifier = Modifier.background(FirefoxTheme.colors.layer1)
                .fillMaxSize(),
        ) {
            PrivateBrowsingDescription(
                onLearnMoreClick = {},
            )
        }
    }
}
