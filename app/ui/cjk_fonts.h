#pragma once

namespace bt::ui {
    /**
     * @brief Merges a CJK-capable system font into the fonts loaded by grey (regular, bold and monospace),
     *        so that Chinese UI text and Chinese browser/profile names render instead of "?".
     *
     * The font family follows the active UI language (traditional Chinese prefers Microsoft JhengHei / PingFang TC).
     * Must be called after grey has loaded its fonts and before the first frame. Safe to call more than once:
     * it does nothing if the current font atlas already has the CJK font merged.
     */
    void merge_cjk_fonts();

    /**
     * @brief Reads the CJK font files merge_cjk_fonts() will use into the process-lifetime cache, so the first window
     *        does not wait for ~20MB of disk reads. Does not need an ImGui context.
     */
    void prewarm_cjk_fonts();
}
