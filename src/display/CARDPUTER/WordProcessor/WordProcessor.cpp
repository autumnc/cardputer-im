#include "WordProcessor.h"
#include "app/app.h"

//
#include "service/Editor/Editor.h"
#include "service/Tools/Tools.h"
#include "keyboard/keyboard.h"
#include "display/display.h"

//
#include "display/CARDPUTER/display_CARDPUTER.h"
#include <u8g2_fonts.h>

#ifdef USE_IME
#include "service/IME/IME.h"
#endif

extern const uint8_t u8g2_font_terminus28_tf[];
extern const uint8_t u8g2_font_term_cjk_28[];

//
int screen_width = 240;
int screen_height = 135;

//
// FONT SIZE LEVELS (Ctrl-'+' / Ctrl-'-' cycle through these).
//
// Each level pairs a monospaced Latin font (profont) with a matching CJK
// bitmap font (efontCN), plus the coupled cell metrics. A CJK glyph is drawn
// double-width (cjk_width = 2*font_width) so it stays squared with the Latin
// grid. Metrics follow the original profont12 pattern: font_width = glyph_w+1
// (tracking), font_height = glyph_h+1 (leading), font_baseline = glyph_h -
// Font metrics (single fixed size).
static int font_level = 0;
int font_width = 14;
int font_height = 28;
int font_baseline = 22;
int cjk_width = 28;

static lgfx::U8g2font g_term_cjk(u8g2_font_term_cjk_28);
static lgfx::U8g2font g_profont22(u8g2_font_terminus28_tf);
static const lgfx::IFont *g_cjkFont = &g_term_cjk;

// Layout constants (px).
const int STATUSBAR_H = 18;
const int STATUSBAR_Y = screen_height - STATUSBAR_H; // 117

// The candidate bar has a fixed 2-line reserved area just above the status bar.
int candidateBarH = font_height + 4; // ~32px, single row
int candidateBarRowH = candidateBarH;
int candidateBarY = STATUSBAR_Y - candidateBarH; // ~85

// The bottom text row sits just above the reserved candidate bar.
int editY = candidateBarY - font_height; // 61-28=33
const int LINES_BELOW_CURSOR = 0;
int cursorRowY = editY;
int imeBarY = candidateBarY;
const int cursorHeight = 2;

// Some flags
bool clear_background = true;
// When true, only the current edit line's strip is repainted (no full-screen
// wipe). This is the common typing case and avoids the whole-screen flash.
bool clear_editline = false;
unsigned int last_sleep = millis();

// Initialise the fixed font metrics and editor grid at power-on.
void WP_init_fonts()
{
    // Reserve 2-line candidate bar + status bar at the bottom.
    candidateBarH = font_height + 4;
    candidateBarRowH = candidateBarH;
    candidateBarY = STATUSBAR_Y - candidateBarH;
    editY = candidateBarY - font_height;
    cursorRowY = editY;
    if (cursorRowY < 0) cursorRowY = 0;
    imeBarY = candidateBarY;


    int cols = screen_width / font_width;
    int rows = cursorRowY / font_height;
    if (rows < 1) rows = 1;
    Editor::getInstance().cols = cols;
    Editor::getInstance().rows = rows;
    Editor::getInstance().updateScreen();

    clear_background = true;
}

// Y at which the edit/cursor line renders. Normally cursorRowY (LINES_BELOW_
// CURSOR rows up from the bottom), so following lines show below. But near the
// END of the document there aren't that many following lines, so the cursor
// line is allowed to drop toward the bottom by the shortfall - at true EOF it
// reaches editY. This lets the cursor move down into the bottom rows instead of
// being stuck LINES_BELOW_CURSOR rows up with empty rows beneath it.
static int WP_cursor_row_y()
{
    int cursorLine = Editor::getInstance().cursorLine;
    int totalLine = Editor::getInstance().totalLine;
    int linesBelow = totalLine - cursorLine;
    if (linesBelow < 0)
        linesBelow = 0;
    if (linesBelow > LINES_BELOW_CURSOR)
        linesBelow = LINES_BELOW_CURSOR;
    return cursorRowY + (LINES_BELOW_CURSOR - linesBelow) * font_height;
}

//
void WP_setup()
{
    // Editor Init - setup screen size with the fixed font metrics.
    WP_init_fonts();

    // setup default color
    JsonDocument &app = status();

    // load file from the editor
    int file_index = app["config"]["file_index"].as<int>();
    String filename = format("/%d.txt", file_index);
    _log("[WP_setup] load file [%s]\n", filename.c_str());
    Editor::getInstance().loadFile(filename);

    //
    if (!app["config"]["foreground_color"].is<int>())
    {
        app["config"]["foreground_color"] = TFT_WHITE;
    }

    // start from clear background
    clear_background = true;

    // sleep timer reset
    last_sleep = millis();
}

//
void WP_render()
{
    // Acquire Editor lock to prevent race with Core 0 keyboard updates
    Editor::Lock guard(Editor::getInstance());

    // the editor swapped to a different window of the file - force a full redraw
    if (Editor::getInstance().pageChanged)
    {
        Editor::getInstance().pageChanged = false;
        clear_background = true;
    }

    // timers
    WP_check_saved();

    // CLEAR BACKGROUND
    WP_render_clear();

    // Whether this frame is doing a full redraw. Captured now because
    // WP_render_ime() (below) may REQUEST a full redraw for the NEXT frame by
    // setting clear_background = true (when composition just ended and the bar
    // needs erasing); that request must survive to next frame, so we only clear
    // the flag if it was already set at the start of this frame.
    bool full_redraw_this_frame = clear_background;

    // RENDER TEXT
    WP_render_text();

    // BLINK CURSOR
    WP_render_cursor();

    // STATUS
    WP_render_status();

    // CHINESE IME CANDIDATE BAR (drawn last so it sits on top)
    WP_render_ime();

    if (full_redraw_this_frame)
        clear_background = false;

    // Editor House Keeping Tasks
    Editor::getInstance().loop();
}

//
// Wubi IME candidate bar.
//
// While the user is composing a Wubi code, an overlay strip (imeBarY == the
// bottom row) is drawn showing the typed code and the numbered candidate hanzi.
// No row is permanently reserved for it, so when idle the document uses the full
// screen height. When composition clears, the strip is wiped and the document
// line it overlaid is repainted.
void WP_render_ime()
{
#ifdef USE_IME
    JsonDocument &app = status();
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    IME &ime = IME::getInstance();

    static bool was_composing = false;
    bool composing = ime.active() && ime.composing();

    // The candidate bar has a fixed reserved area at candidateBarY just
    // above the status bar. Text never overlaps it.
    const int barH = candidateBarH;
    const int barY = candidateBarY;

    if (!composing)
    {
        if (was_composing)
        {
            was_composing = false;
            // Wipe the bar area back to document background.
            M5Cardputer.Display.fillRect(0, barY, screen_width, barH, background_color);
        }
        return;
    }

    // Repaint the bar ONLY when its content changes.
    String signature = ime.composition();
    signature += '\x1f';
    {
        const std::vector<String> &c = ime.candidates();
        for (size_t i = 0; i < c.size(); i++)
        {
            signature += c[i];
            signature += '\x1f';
        }
    }

    static String signature_prev;
    if (was_composing && signature == signature_prev)
        return;

    was_composing = true;
    signature_prev = signature;

    // background strip extending to status bar (no gap)
    M5Cardputer.Display.fillRect(0, barY, screen_width, STATUSBAR_Y - barY, foreground_color);

    // Display code: selected hanzi + remaining pinyin (逐字 composing)
    M5Cardputer.Display.setFont(&g_profont22);
    M5Cardputer.Display.setTextColor(background_color, foreground_color);
    String code = ime.displayCode();
    // 两分 mode indicator
    if (code.length() == 0 && ime.composing())
    {
        M5Cardputer.Display.drawString("LF:", 2, barY);
    }
    // Draw selected hanzi in CJK font, remaining pinyin in Latin font
    String pinyin = ime.composition(); // just the pinyin part
    String prefix = code.substring(0, code.length() - pinyin.length());
    int cx = 2;
    if (prefix.length() > 0)
    {
        M5Cardputer.Display.setFont(g_cjkFont);
        M5Cardputer.Display.drawString(prefix, cx, barY + 1);
        cx += (prefix.length() / 3) * cjk_width; // UTF-8 CJK: 3 bytes each
        M5Cardputer.Display.setFont(&g_profont22);
    }
    M5Cardputer.Display.drawString(pinyin, cx, barY);

    // Page size: account for CJK prefix width (each hanzi = cjk_width)
    int codePx = 8;
    for (size_t ci = 0; ci < code.length(); ) {
        if ((uint8_t)code[ci] >= 0x80) { codePx += cjk_width; ci += 3; }
        else { codePx += font_width; ci++; }
    }
    int avail = screen_width - codePx - 4;
    int slot = font_width + cjk_width;
    int perPage = avail / slot;
    if (perPage < 1) perPage = 1;
    if (perPage > 9) perPage = 9;
    static int lastN = 0;
    if (perPage != lastN) { lastN = perPage; IME::getInstance().setPageSize(perPage); }

    // 1-row candidate rendering
    const std::vector<String> &cands = ime.candidates();
    int x = codePx;
    for (size_t i = 0; i < cands.size(); i++)
    {
        int adv = ((cands[i].length() + 2) / 3) * cjk_width;
        if (x + adv > screen_width - 4) break;
        M5Cardputer.Display.setFont(&g_profont22);
        M5Cardputer.Display.drawString(String((int)i + 1), x, barY + 1);
        x += font_width;
        M5Cardputer.Display.setFont(g_cjkFont);
        M5Cardputer.Display.drawString(cands[i], x, barY + 1);
        x += adv;
    }

    M5Cardputer.Display.setFont(&g_profont22);
    M5Cardputer.Display.setTextColor(foreground_color, background_color);
#endif
}

//
void WP_render_text()
{
    JsonDocument &app = status();

    // LOAD COLORS
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    // SET FONT
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(foreground_color, background_color);
    M5Cardputer.Display.setFont(&g_profont22);

    // Cursor Information
    static int cursorLine_prev = 0;
    static int cursorLinePos_prev = 0;
    int cursorLine = Editor::getInstance().cursorLine;
    int cursorLinePos = Editor::getInstance().cursorLinePos;
    int totalLine = Editor::getInstance().totalLine;

    //
    // Middle part of the text will be rendered
    // Only when refresh background is called
    //
    // initiate sprite
    if (clear_background)
    {
        int editLineY = WP_cursor_row_y(); // usually cursorRowY, lower near EOF

        // Scrollback lines are anchored UPWARD from the edit line (line i at
        // editLineY - (cursorLine - i)*font_height, gap always font_height).
        // Walk up until we run off the top of the screen or out of lines - this
        // fills the top rows even when the edit line has dropped toward the
        // bottom near EOF (using a fixed row count left blank rows at the top).
        for (int i = cursorLine - 1; i >= 0; i--)
        {
            int y = editLineY - (cursorLine - i) * font_height;
            if (y < 0)
                break;
            WP_render_line(i, y);
        }

        // the edit line itself
        WP_render_line(cursorLine, editLineY);

        // following lines below the cursor (down to the bottom row)
        for (int i = cursorLine + 1; i <= totalLine; i++)
        {
            int y = editLineY + (i - cursorLine) * font_height;
            if (y > editY)
                break;
            WP_render_line(i, y);
        }
    }
    else if (clear_editline)
    {
        // Only the edit line changed - repaint just that line. Doing this only
        // on an actual change (not every idle frame) stops the shimmer.
        WP_render_line(cursorLine, WP_cursor_row_y());
    }
}

//
// Advance in pixels of the glyph that starts at byte `line[i]`.
// ASCII / Latin-1 advance one cell; multi-byte UTF-8 (CJK) advance two.
static int WP_glyph_width(const char *line, int i)
{
    return utf8_char_len((uint8_t)line[i]) >= 2 ? cjk_width : font_width;
}

// Pixel X of the glyph boundary that sits `byteOffset` bytes into the line.
// Used by the cursor so it lands between glyphs, not between UTF-8 bytes.
int WP_line_width_bytes(const char *line, int byteOffset)
{
    int x = 0;
    int i = 0;
    while (i < byteOffset)
    {
        int len = utf8_char_len((uint8_t)line[i]);
        x += (len >= 2) ? cjk_width : font_width;
        i += len;
    }
    return x;
}

//
//
void WP_render_line(int line_num, int y)
{
    char *line = Editor::getInstance().linePositions[line_num];
    int length = Editor::getInstance().lineLengths[line_num];

    // render, walking the line one UTF-8 character at a time
    int x = 0;
    int i = 0;
    while (i < length)
    {
        uint8_t value = (uint8_t)line[i];

        // newline is a layout marker, never drawn
        if (value == '\n')
        {
            i += 1;
            continue;
        }

        int clen = utf8_char_len(value);

        // plain ASCII - keep the existing crisp monospaced glyph
        if (clen == 1 && value < 0x80)
        {
            M5Cardputer.Display.drawChar((char)value, x, y + font_baseline);
            x += font_width;
            i += 1;
            continue;
        }

#ifdef USE_IME
        // a multi-byte UTF-8 run (CJK and friends): draw it with the
        // built-in Chinese font, then restore the body font. Only compiled for
        // the Chinese-enabled build so the plain Cardputer firmware does not
        // link the (large) CJK glyph data.
        if (clen >= 2 && i + clen <= length)
        {
            char glyph[5];
            memcpy(glyph, line + i, clen);
            glyph[clen] = '\0';

            M5Cardputer.Display.setFont(g_cjkFont);
            M5Cardputer.Display.drawString(glyph, x, y);
            M5Cardputer.Display.setFont(&g_profont22);

            x += cjk_width;
            i += clen;
            continue;
        }
#endif

        // stray high byte (e.g. legacy Latin-1 content) - best-effort
        String str = asciiToUnicode(value);
        if (str.length() == 0)
            M5Cardputer.Display.drawChar((char)value, x, y + font_baseline);
        else
            M5Cardputer.Display.drawString(str, x, y);
        x += font_width;
        i += 1;
    }
}

//
// Render Cursor
void WP_render_cursor()
{
    JsonDocument &app = status();

    // retrieve color information
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    // Cursor information. cursorLinePos is a BYTE offset within the line;
    // convert it to pixels through the UTF-8 aware width helper so the cursor
    // lands on a glyph boundary instead of inside a multi-byte character.
    static int cursorX_prev = 0;
    int cursorLinePos = Editor::getInstance().cursorLinePos;
    int cursorLine = Editor::getInstance().cursorLine;
    int cursorPos = Editor::getInstance().cursorPos;

    char *line = Editor::getInstance().linePositions[cursorLine];

    // Calculate Cursor X position
    int cursorX = 0;
    if (Editor::getInstance().buffer[cursorPos - 1] != '\n' && cursorLinePos != 0)
    {
        cursorX = WP_line_width_bytes(line, cursorLinePos);
    }

    // The underline width matches the glyph the cursor sits under, so a hanzi
    // gets a full double-width underline.
    int cursorW = font_width;
    if (cursorLinePos < Editor::getInstance().lineLengths[cursorLine])
        cursorW = WP_glyph_width(line, cursorLinePos);

    // The cursor underline sits under the edit line, which is not pinned to the
    // bottom (it drops toward editY near EOF - see WP_cursor_row_y).
    int cursorLineY = WP_cursor_row_y() + font_height - 1;

    // Blink the cursor every 500 ms
    static bool blink = false;
    static unsigned int last = millis();
    if (millis() - last > 500)
    {
        last = millis();
        blink = !blink;
    }

    // Erase the previous cursor underline if it moved (in X or Y).
    static int cursorY_prev = cursorLineY;
    if (cursorX != cursorX_prev || cursorLineY != cursorY_prev)
    {
        M5Cardputer.Display.fillRect(cursorX_prev, cursorY_prev, cjk_width, cursorHeight, background_color);
        cursorX_prev = cursorX;
        cursorY_prev = cursorLineY;
    }

    if (blink)
        M5Cardputer.Display.fillRect(cursorX, cursorLineY, cursorW, cursorHeight, foreground_color);
    else
        M5Cardputer.Display.fillRect(cursorX, cursorLineY, cursorW, cursorHeight, background_color);
}

//
void WP_render_status()
{
    JsonDocument &app = status();

    // LOAD COLORS
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    // On full redraw, wipe the bar to cover candidate-bar bleed-through
    if (clear_background)
        M5Cardputer.Display.fillRect(0, STATUSBAR_Y, screen_width, STATUSBAR_H, background_color);

    // Only repaint each status element when its value changes (or on a full
    // redraw). Repainting the whole bar every 150 ms frame was a flicker source.
    static int fileIndex_prev = -1;
    static int wordCount_prev = -1;

    // file index number
    int file_index = app["config"]["file_index"].as<int>();
    if (file_index != fileIndex_prev || clear_background)
    {
        fileIndex_prev = file_index;
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
        M5Cardputer.Display.setTextColor(background_color, foreground_color);
        M5Cardputer.Display.fillRect(4, STATUSBAR_Y, 24, 16, foreground_color);
        M5Cardputer.Display.drawString(String(file_index), 4, STATUSBAR_Y);
    }

    // WORD COUNT
    int wordCount = Editor::getInstance().wordCountFile + Editor::getInstance().wordCountBuffer;
    if (wordCount != wordCount_prev || clear_background)
    {
        wordCount_prev = wordCount;
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
        M5Cardputer.Display.setTextColor(foreground_color, background_color);
        M5Cardputer.Display.fillRect(30, STATUSBAR_Y, 60, 16, background_color);
        M5Cardputer.Display.drawString(formatNumber(wordCount), 30, STATUSBAR_Y);
    }

#ifdef USE_IME
    // INPUT-MODE INDICATOR: [五] when the Wubi IME is on, [En] otherwise.
    // Sits between the word count and the battery %. Only repainted when the
    // mode actually changes (or on a full redraw) so it never flickers.
    {
        const int IME_X = 95;
        static int ime_prev = -1;
        int ime_now = IME::getInstance().active() ? 1 : 0;

        if (ime_now != ime_prev || clear_background)
        {
            ime_prev = ime_now;

            // clear the cell first so the previous label doesn't bleed through
            M5Cardputer.Display.fillRect(IME_X, STATUSBAR_Y, 34, 16, background_color);
            M5Cardputer.Display.setTextColor(foreground_color, background_color);

            if (ime_now)
            {
                // Scheme abbreviation in AsciiFont8x16 (no CJK font needed).
                const char *label = "Wu";
                switch (IME::getInstance().scheme())
                {
                case IME::PINYIN:    label = "Py"; break;
                case IME::SHUANGPIN: label = "Sh"; break;
                default:             label = "Wu"; break;
                }
                M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
                M5Cardputer.Display.drawString("[", IME_X, STATUSBAR_Y);
                M5Cardputer.Display.drawString(label, IME_X + 8, STATUSBAR_Y);
                M5Cardputer.Display.drawString("]", IME_X + 24, STATUSBAR_Y);
            }
            else
            {
                M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
                M5Cardputer.Display.drawString("[En]", IME_X, STATUSBAR_Y);
            }
        }
    }
#endif

    // SAVE STATUS
    static int saved_prev = -1;
    int saved_now = Editor::getInstance().saved ? 1 : 0;
    if (saved_now != saved_prev || clear_background)
    {
        saved_prev = saved_now;
        M5Cardputer.Display.fillCircle(screen_width - 15, STATUSBAR_Y + 8, 5,
                                       saved_now ? TFT_GREEN : TFT_RED);
        M5Cardputer.Display.drawCircle(screen_width - 15, STATUSBAR_Y + 8, 5, TFT_BLACK);
    }

    // BATTERY
    static int displayedBattery = -1;     // the value shown on screen
    static int lastReadBattery = -1;      // last raw value read
    static uint32_t changeDetectedAt = 0; // when the change was first noticed

    int current = M5Cardputer.Power.getBatteryLevel();

    // First run
    if (displayedBattery < 0)
    {
        displayedBattery = current;
        lastReadBattery = current;
    }

    // If battery reading has not changed, reset timer & do nothing
    if (current == displayedBattery)
    {
        changeDetectedAt = 0;
        lastReadBattery = current;
    }
    // Battery reading changed (ex: 100 → 99)
    else
    {
        // If a change was just detected, start the timer
        if (changeDetectedAt == 0)
        {
            changeDetectedAt = millis();
            lastReadBattery = current;
        }

        // If reading fluctuates (ex: 100 → 99 → 100), cancel
        if (current != lastReadBattery)
        {
            changeDetectedAt = millis(); // restart timer with new reading
            lastReadBattery = current;
        }

        // If a second passed with stable new value → update UI
        if (millis() - changeDetectedAt >= 1000)
        {
            displayedBattery = current;
            changeDetectedAt = 0;
        }
    }

    // Draw smoothed / stabilized value, only when it changes
    static int battery_prev = -999;
    if (displayedBattery != battery_prev || clear_background)
    {
        battery_prev = displayedBattery;
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
        M5Cardputer.Display.setTextColor(foreground_color, background_color);
        M5Cardputer.Display.fillRect(screen_width - 85, STATUSBAR_Y, 40, 16, background_color);
        M5Cardputer.Display.drawString(
            format("%d%%", displayedBattery),
            screen_width - 85,
            STATUSBAR_Y);
    }
}

//
// Clear Screen
// Do it as less as possible so that there is the least amount of the flicker
//
void WP_render_clear()
{
    //
    JsonDocument &app = status();

    // LOAD COLORS
    uint16_t background_color = app["config"]["background_color"].as<uint16_t>();
    uint16_t foreground_color = app["config"]["foreground_color"].as<uint16_t>();

    //
    static int cursorLine_prev = 0;
    static int cursorPos_prev = 0;
    int cursorLine = Editor::getInstance().cursorLine;
    int cursorPos = Editor::getInstance().cursorPos;

    //
    static int bufferSize_prev = 0;
    static int totalLine_prev = 0;
    int bufferSize = Editor::getInstance().getBufferSize();
    int totalLine = Editor::getInstance().totalLine;

    // start each frame assuming nothing needs erasing
    clear_editline = false;

    // A change of line (arrow to another row, paging) OR a change in the number
    // of lines (delete/join a line, word-wrap adding/removing a row, paste)
    // needs the whole text area redrawn - otherwise lines that shift up/down
    // leave stale copies behind (the deleted line "overlays").
    if (cursorLine_prev != cursorLine || totalLine_prev != totalLine)
    {
        clear_background = true;
        cursorLine_prev = cursorLine;
        totalLine_prev = totalLine;
    }

    // Any edit or cursor move that stays on the same line only needs that one
    // line repainted. Repainting a single strip - instead of wiping the whole
    // screen every keystroke - is what removes the typing flash.
    else if (cursorPos_prev != cursorPos || bufferSize_prev != bufferSize)
    {
        clear_editline = true;
    }

    cursorPos_prev = cursorPos;
    bufferSize_prev = bufferSize;
    totalLine_prev = totalLine;

    // FULL CLEAR
    if (clear_background)
    {
        M5Cardputer.Display.fillRect(
            0,
            0,
            M5Cardputer.Display.width(),
            M5Cardputer.Display.height(),
            background_color);
    }
    // EDIT-LINE CLEAR: wipe just the strip the edit line occupies (including
    // the cursor underline row) so stale glyphs don't linger after a backspace.
    // Must use the edit line's ACTUAL Y (WP_cursor_row_y), which drops toward
    // the bottom near EOF - clearing the fixed cursorRowY there erased a
    // higher scrollback line (e.g. line 6) that was never repainted.
    else if (clear_editline)
    {
        M5Cardputer.Display.fillRect(
            0,
            WP_cursor_row_y(),
            M5Cardputer.Display.width(),
            font_height + cursorHeight + 1,
            background_color);
    }
}

//
//
void WP_keyboard(int key, bool pressed, int index)
{
    // ignore non pritable keys
    if (key == 0)
        return;

    JsonDocument &app = status();
    _debug("WP_keyboard key: %d, pressed: %d\n", key, pressed);

    // C-s: save in place without leaving the editor (unlike ESC, which saves
    // and opens the menu). Handle it here so it never reaches the editor buffer.
    if (key == KEY_SAVE)
    {
        if (!pressed)
            Editor::getInstance().saveFile();
        return;
    }


    if (key == 27 || key == 6)
    {
        if (!pressed)
        {
            // Save before transitioning to the menu
            Editor::getInstance().saveFile();

            // open menu
            _debug("WP_keyboard - Received ESC Key\n");
            app["screen"] = MENUSCREEN;
        }

        // ESC button is ignored
        return;
    }

    // Check if File Change request is pressed
    if (key >= 1000 && key <= 1010)
    {
        if (!pressed)
        {
            int fileIndex = key - 1000;
            _log("File Change Requested: %d\n", fileIndex);

            //
            Editor::getInstance().saveFile();

            // save config
            app["config"]["file_index"] = fileIndex;
            config_save();

            // load new file
            Editor::getInstance().loadFile(format("/%d.txt", fileIndex));
        }
    }

    else
    {
        // send the keys to the editor
        Editor::getInstance().keyboard(key, pressed);
    }
}

//
// Check if text is saved.  Autosave fires after 3 s of idle, but never
// while the IME candidate bar is visible (composing would be interrupted).
void WP_check_saved()
{
    static unsigned int last = millis();
    static int lastBufferSize = Editor::getInstance().getBufferSize();
    int bufferSize = Editor::getInstance().getBufferSize();

    if (lastBufferSize != bufferSize)
    {
        last = millis();
        lastBufferSize = bufferSize;
    }

    if (millis() - last > 3000)
    {
        last = millis();

#ifdef USE_IME
        // Don't save while the user is composing - the candidate bar is
        // showing and the typed code would be lost.
        if (IME::getInstance().active() && IME::getInstance().composing())
            return;
#endif

        if (!Editor::getInstance().saved)
            Editor::getInstance().saveFile();
    }
}
