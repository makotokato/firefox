/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.ui.tabcounter

import android.animation.AnimatorSet
import android.animation.ObjectAnimator
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Typeface
import android.util.AttributeSet
import android.util.TypedValue
import android.view.LayoutInflater
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.RelativeLayout
import android.widget.TextView
import androidx.annotation.VisibleForTesting
import androidx.appcompat.content.res.AppCompatResources
import androidx.core.view.isVisible
import mozilla.components.support.utils.DrawableUtils
import mozilla.components.ui.tabcounter.databinding.MozacUiTabcounterLayoutBinding
import java.text.NumberFormat

class TabCounterView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyle: Int = 0,
) : RelativeLayout(context, attrs, defStyle) {

    private val animationSet: AnimatorSet
    private var binding: MozacUiTabcounterLayoutBinding
    private var counterBox: ImageView
    private var counterText: TextView
    private var counterRoot: FrameLayout
    private var counterMask: ImageView
    private var counterColor: ColorStateList? = null

    init {
        binding = MozacUiTabcounterLayoutBinding.inflate(LayoutInflater.from(context), this)
        counterBox = binding.counterBox
        counterText = binding.counterText
        counterRoot = binding.counterRoot
        counterMask = binding.counterMask

        setCount(internalCount)

        context.obtainStyledAttributes(attrs, R.styleable.TabCounterView, defStyle, 0).apply {
            counterColor = getColorStateList(
                R.styleable.TabCounterView_tabCounterTintColor,
            ) ?: AppCompatResources.getColorStateList(context, R.color.mozac_ui_tabcounter_default_tint)

            counterColor?.let {
                setColor(it)
            }

            clipChildren = false

            recycle()
        }

        animationSet = createAnimatorSet()
    }

    /**
     * Sets the colors of the tab counter box and text.
     */
    @VisibleForTesting
    internal fun setColor(colorStateList: ColorStateList) {
        val tabCounterBox =
            DrawableUtils.loadAndTintDrawable(context, R.drawable.mozac_ui_tabcounter_box, colorStateList)
        counterBox.setImageDrawable(tabCounterBox)
        counterText.setTextColor(colorStateList)
    }

    /**
     * Updates the content description of the tab counter.
     * @param isPrivate [Boolean] used to determine whether to set the
     * private or normal mode content description.
     */
    fun updateContentDescription(isPrivate: Boolean) {
        contentDescription = if (isPrivate) {
            context.getString(R.string.mozac_tab_counter_private, internalCount.toString())
        } else {
            context.getString(R.string.mozac_tab_counter_open_tab_tray, internalCount.toString())
        }
    }

    fun setCountWithAnimation(count: Int) {
        setCount(count)

        // No need to animate on these cases.
        when {
            internalCount == 0 -> return // Initial state.
            internalCount == count -> return // There isn't any tab added or removed.
            internalCount > MAX_VISIBLE_TABS -> return // There are still over MAX_VISIBLE_TABS tabs open.
        }

        // Cancel previous animations if necessary.
        if (animationSet.isRunning) {
            animationSet.cancel()
        }
        // Trigger animations.
        animationSet.start()
    }

    /**
     * Toggles the visibility of the mask overlay on the counter
     *
     * @param showMask [Boolean] used to determine whether to show or hide the mask.
     */
    fun toggleCounterMask(showMask: Boolean) {
        counterMask.isVisible = showMask
    }

    fun setCount(count: Int) {
        internalCount = count
        setBackgroundDrawable(count)
        setCounterText(count)
    }

    private fun setBackgroundDrawable(count: Int) {
        val drawableRes = when (count > MAX_VISIBLE_TABS) {
            true -> R.drawable.mozac_ui_infinite_tabcounter_box
            false -> R.drawable.mozac_ui_tabcounter_box
        }

        val currentCounterColor = counterColor
        val backgroundDrawable = when (currentCounterColor != null) {
            true -> DrawableUtils.loadAndTintDrawable(context, drawableRes, currentCounterColor)
            false -> AppCompatResources.getDrawable(context, drawableRes)
        }

        backgroundDrawable?.let { counterBox.background = it }
    }

    private fun setCounterText(count: Int) {
        when (count > MAX_VISIBLE_TABS) {
            true -> counterText.isVisible = false
            false -> {
                counterText.isVisible = true
                adjustTextSize(count)
                counterText.text = NumberFormat.getInstance().format(count.toLong())
            }
        }
    }

    private fun createAnimatorSet(): AnimatorSet {
        val animatorSet = AnimatorSet()
        createBoxAnimatorSet(animatorSet)
        createTextAnimatorSet(animatorSet)
        return animatorSet
    }

    private fun createBoxAnimatorSet(animatorSet: AnimatorSet) {
        // The first animator, fadeout in 33 ms (49~51, 2 frames).
        val fadeOut = ObjectAnimator.ofFloat(
            counterBox,
            "alpha",
            ANIM_BOX_FADEOUT_FROM,
            ANIM_BOX_FADEOUT_TO,
        ).setDuration(ANIM_BOX_FADEOUT_DURATION)

        // Move up on y-axis, from 0.0 to -5.3 in 50ms, with fadeOut (49~52, 3 frames).
        val moveUp1 = ObjectAnimator.ofFloat(
            counterBox,
            "translationY",
            ANIM_BOX_MOVEUP1_TO,
            ANIM_BOX_MOVEUP1_FROM,
        ).setDuration(ANIM_BOX_MOVEUP1_DURATION)

        // Move down on y-axis, from -5.3 to -1.0 in 116ms, after moveUp1 (52~59, 7 frames).
        val moveDown2 = ObjectAnimator.ofFloat(
            counterBox,
            "translationY",
            ANIM_BOX_MOVEDOWN2_FROM,
            ANIM_BOX_MOVEDOWN2_TO,
        ).setDuration(ANIM_BOX_MOVEDOWN2_DURATION)

        // FadeIn in 66ms, with moveDown2 (52~56, 4 frames).
        val fadeIn = ObjectAnimator.ofFloat(
            counterBox,
            "alpha",
            ANIM_BOX_FADEIN_FROM,
            ANIM_BOX_FADEIN_TO,
        ).setDuration(ANIM_BOX_FADEIN_DURATION)

        // Move down on y-axis, from -1.0 to 2.7 in 116ms, after moveDown2 (59~66, 7 frames).
        val moveDown3 = ObjectAnimator.ofFloat(
            counterBox,
            "translationY",
            ANIM_BOX_MOVEDOWN3_FROM,
            ANIM_BOX_MOVEDOWN3_TO,
        ).setDuration(ANIM_BOX_MOVEDOWN3_DURATION)

        // Move up on y-axis, from 2.7 to 0 in 133ms, after moveDown3 (66~74, 8 frames).
        val moveUp4 = ObjectAnimator.ofFloat(
            counterBox,
            "translationY",
            ANIM_BOX_MOVEDOWN4_FROM,
            ANIM_BOX_MOVEDOWN4_TO,
        ).setDuration(ANIM_BOX_MOVEDOWN4_DURATION)

        // Scale up height from 2% to 105% in 100ms, after moveUp1 and delay 16ms (53~59, 6 frames).
        val scaleUp1 = ObjectAnimator.ofFloat(
            counterBox,
            "scaleY",
            ANIM_BOX_SCALEUP1_FROM,
            ANIM_BOX_SCALEUP1_TO,
        ).setDuration(ANIM_BOX_SCALEUP1_DURATION)
        scaleUp1.startDelay = ANIM_BOX_SCALEUP1_DELAY // delay 1 frame after moveUp1

        // Scale down height from 105% to 99% in 116ms, after scaleUp1 (59~66, 7 frames).
        val scaleDown2 = ObjectAnimator.ofFloat(
            counterBox,
            "scaleY",
            ANIM_BOX_SCALEDOWN2_FROM,
            ANIM_BOX_SCALEDOWN2_TO,
        ).setDuration(ANIM_BOX_SCALEDOWN2_DURATION)

        // Scale up height from 99% to 100% in 133ms, after scaleDown2 (66~74, 8 frames).
        val scaleUp3 = ObjectAnimator.ofFloat(
            counterBox,
            "scaleY",
            ANIM_BOX_SCALEUP3_FROM,
            ANIM_BOX_SCALEUP3_TO,
        ).setDuration(ANIM_BOX_SCALEUP3_DURATION)

        animatorSet.play(fadeOut).with(moveUp1)
        animatorSet.play(moveUp1).before(moveDown2)
        animatorSet.play(moveDown2).with(fadeIn)
        animatorSet.play(moveDown2).before(moveDown3)
        animatorSet.play(moveDown3).before(moveUp4)

        animatorSet.play(moveUp1).before(scaleUp1)
        animatorSet.play(scaleUp1).before(scaleDown2)
        animatorSet.play(scaleDown2).before(scaleUp3)
    }

    private fun createTextAnimatorSet(animatorSet: AnimatorSet) {
        val firstAnimator = animatorSet.childAnimations[0]

        // Fadeout in 100ms, with firstAnimator (49~51, 2 frames).
        val fadeOut = ObjectAnimator.ofFloat(
            counterText,
            "alpha",
            ANIM_TEXT_FADEOUT_FROM,
            ANIM_TEXT_FADEOUT_TO,
        ).setDuration(ANIM_TEXT_FADEOUT_DURATION)

        // FadeIn in 66 ms, after fadeOut with delay 96ms (57~61, 4 frames).
        val fadeIn = ObjectAnimator.ofFloat(
            counterText,
            "alpha",
            ANIM_TEXT_FADEIN_FROM,
            ANIM_TEXT_FADEIN_TO,
        ).setDuration(ANIM_TEXT_FADEIN_DURATION)
        fadeIn.startDelay = (ANIM_TEXT_FADEIN_DELAY) // delay 6 frames after fadeOut

        // Move down on y-axis, from 0 to 4.4 in 66ms, with fadeIn (57~61, 4 frames).
        val moveDown = ObjectAnimator.ofFloat(
            counterText,
            "translationY",
            ANIM_TEXT_MOVEDOWN_FROM,
            ANIM_TEXT_MOVEDOWN_TO,
        ).setDuration(ANIM_TEXT_MOVEDOWN_DURATION)
        moveDown.startDelay = (ANIM_TEXT_MOVEDOWN_DELAY) // delay 6 frames after fadeOut

        // Move up on y-axis, from 0 to 4.4 in 66ms, after moveDown (61~69, 8 frames).
        val moveUp = ObjectAnimator.ofFloat(
            counterText,
            "translationY",
            ANIM_TEXT_MOVEUP_FROM,
            ANIM_TEXT_MOVEUP_TO,
        ).setDuration(ANIM_TEXT_MOVEUP_DURATION)

        animatorSet.play(firstAnimator).with(fadeOut)
        animatorSet.play(fadeOut).before(fadeIn)
        animatorSet.play(fadeIn).with(moveDown)
        animatorSet.play(moveDown).before(moveUp)
    }

    private fun adjustTextSize(newCount: Int) {
        val newRatio = if (newCount in TWO_DIGITS_TAB_COUNT_THRESHOLD..MAX_VISIBLE_TABS) {
            TWO_DIGITS_SIZE_RATIO
        } else {
            ONE_DIGIT_SIZE_RATIO
        }

        val counterBoxWidth =
            context.resources.getDimensionPixelSize(R.dimen.mozac_tab_counter_box_width_height)
        val textSize = newRatio * counterBoxWidth
        counterText.setTextSize(TypedValue.COMPLEX_UNIT_PX, textSize)
        counterText.setTypeface(null, Typeface.BOLD)
        counterText.setPadding(0, 0, 0, 0)
    }

    companion object {
        var internalCount = 0

        const val MAX_VISIBLE_TABS = 99

        const val ONE_DIGIT_SIZE_RATIO = 0.5f
        const val TWO_DIGITS_SIZE_RATIO = 0.4f
        const val TWO_DIGITS_TAB_COUNT_THRESHOLD = 10

        // createBoxAnimatorSet
        private const val ANIM_BOX_FADEOUT_FROM = 1.0f
        private const val ANIM_BOX_FADEOUT_TO = 0.0f
        private const val ANIM_BOX_FADEOUT_DURATION = 33L

        private const val ANIM_BOX_MOVEUP1_FROM = 0.0f
        private const val ANIM_BOX_MOVEUP1_TO = -5.3f
        private const val ANIM_BOX_MOVEUP1_DURATION = 50L

        private const val ANIM_BOX_MOVEDOWN2_FROM = -5.3f
        private const val ANIM_BOX_MOVEDOWN2_TO = -1.0f
        private const val ANIM_BOX_MOVEDOWN2_DURATION = 167L

        private const val ANIM_BOX_FADEIN_FROM = 0.01f
        private const val ANIM_BOX_FADEIN_TO = 1.0f
        private const val ANIM_BOX_FADEIN_DURATION = 66L
        private const val ANIM_BOX_MOVEDOWN3_FROM = -1.0f
        private const val ANIM_BOX_MOVEDOWN3_TO = 2.7f
        private const val ANIM_BOX_MOVEDOWN3_DURATION = 116L

        private const val ANIM_BOX_MOVEDOWN4_FROM = 2.7f
        private const val ANIM_BOX_MOVEDOWN4_TO = 0.0f
        private const val ANIM_BOX_MOVEDOWN4_DURATION = 133L

        private const val ANIM_BOX_SCALEUP1_FROM = 0.02f
        private const val ANIM_BOX_SCALEUP1_TO = 1.05f
        private const val ANIM_BOX_SCALEUP1_DURATION = 100L
        private const val ANIM_BOX_SCALEUP1_DELAY = 16L

        private const val ANIM_BOX_SCALEDOWN2_FROM = 1.05f
        private const val ANIM_BOX_SCALEDOWN2_TO = 0.99f
        private const val ANIM_BOX_SCALEDOWN2_DURATION = 116L

        private const val ANIM_BOX_SCALEUP3_FROM = 0.99f
        private const val ANIM_BOX_SCALEUP3_TO = 1.00f
        private const val ANIM_BOX_SCALEUP3_DURATION = 133L

        // createTextAnimatorSet
        private const val ANIM_TEXT_FADEOUT_FROM = 1.0f
        private const val ANIM_TEXT_FADEOUT_TO = 0.0f
        private const val ANIM_TEXT_FADEOUT_DURATION = 33L

        private const val ANIM_TEXT_FADEIN_FROM = 0.01f
        private const val ANIM_TEXT_FADEIN_TO = 1.0f
        private const val ANIM_TEXT_FADEIN_DURATION = 66L
        private const val ANIM_TEXT_FADEIN_DELAY = 16L * 6

        private const val ANIM_TEXT_MOVEDOWN_FROM = 0.0f
        private const val ANIM_TEXT_MOVEDOWN_TO = 4.4f
        private const val ANIM_TEXT_MOVEDOWN_DURATION = 66L
        private const val ANIM_TEXT_MOVEDOWN_DELAY = 16L * 6

        private const val ANIM_TEXT_MOVEUP_FROM = 4.4f
        private const val ANIM_TEXT_MOVEUP_TO = 0.0f
        private const val ANIM_TEXT_MOVEUP_DURATION = 66L
    }
}
