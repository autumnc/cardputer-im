#include "IME.h"
#include "app/app.h"
#include "display/display.h"
#include <SPIFFS.h>
#include <algorithm>

//
// The dictionary is compiled into flash by board_build.embed_files =
// IME/ime_table.bin (see IME.md). It is read in place (memory-mapped) - there
// is NO SD access for the dictionary, so nothing here can contend with journal
// writes on the SD card. objcopy derives the symbol name from the source PATH
// (non-idents -> '_'), so "IME/ime_table.bin" -> _binary_IME_ime_table_bin_*.
//
static const uint8_t IME_MAGIC[4] = {'I', 'M', 'E', '3'};

extern const uint8_t _binary_IME_ime_table_bin_start[];
extern const uint8_t _binary_IME_ime_table_bin_end[];

// Parse the IME3 header + prefix index from the start of the dictionary blob.
// `total` is the whole (padded) slot size; `_count` bounds the real records.
bool IME::parseHeader(const uint8_t *hdrIndex, size_t total)
{
    if (memcmp(hdrIndex, IME_MAGIC, 4) != 0)
    {
        _log("[IME] bad dictionary magic\n");
        return false;
    }

    _scheme = (Scheme)hdrIndex[4];
    _codeLen = hdrIndex[5];
    if (_codeLen < 1 || _codeLen > MAX_CODE_LEN)
    {
        _log("[IME] bad codeLen %d\n", _codeLen);
        return false;
    }
    _recordSize = _codeLen + HANZI_SIZE + FLAG_SIZE;

    // Max letters the user may type per scheme.
    switch (_scheme)
    {
    case PINYIN:    _maxCode = 11; break;
    case SHUANGPIN: _maxCode = 2; break;
    case WUBI:
    default:        _maxCode = 4; break;
    }

    _count = (uint32_t)hdrIndex[8] | ((uint32_t)hdrIndex[9] << 8) |
             ((uint32_t)hdrIndex[10] << 16) | ((uint32_t)hdrIndex[11] << 24);

    _recordBase = HEADER_SIZE + (size_t)INDEX_ENTRIES * 4;

    size_t need = _recordBase + (size_t)_count * _recordSize;
    if (_count == 0 || need > total)
    {
        _log("[IME] dictionary size mismatch: need %u, have %u\n",
             (unsigned)need, (unsigned)total);
        return false;
    }

    // Copy the prefix index into RAM (little-endian uint32s, 2.7 KB).
    _index.resize(INDEX_ENTRIES);
    for (int k = 0; k < INDEX_ENTRIES; k++)
    {
        const uint8_t *p = hdrIndex + HEADER_SIZE + (size_t)k * 4;
        _index[k] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    // ---- optional word section -------------------------------------------
    // Appended after the single-char records: wordCount[4] + wordIndex[677*4] + wordData[]
    size_t wordBase = _recordBase + (size_t)_count * _recordSize;
    if (wordBase + 4 <= total)
    {
        const uint8_t *wp = hdrIndex + wordBase;
        _wordCount = (uint32_t)wp[0] | ((uint32_t)wp[1] << 8) |
                     ((uint32_t)wp[2] << 16) | ((uint32_t)wp[3] << 24);
        if (_wordCount > 0 && wordBase + 4 + INDEX_ENTRIES * 4 <= total)
        {
            wp += 4;
            _wordIndex.resize(INDEX_ENTRIES);
            for (int k = 0; k < INDEX_ENTRIES; k++)
            {
                const uint8_t *p = wp + (size_t)k * 4;
                _wordIndex[k] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            }
            wp += INDEX_ENTRIES * 4;
            _wordData = wp;
            _wordDataSize = total - (_recordBase + _count * _recordSize)
                            - 4 - INDEX_ENTRIES * 4;

            // ---- prediction section after word data --------------------
            size_t predBase = wordBase + 4 + INDEX_ENTRIES * 4 + _wordDataSize;
            if (predBase + 4 <= total)
            {
                const uint8_t *pp = hdrIndex + predBase;
                _predCount = (uint32_t)pp[0] | ((uint32_t)pp[1] << 8) |
                             ((uint32_t)pp[2] << 16) | ((uint32_t)pp[3] << 24);
                if (_predCount > 0)
                {
                    _predData = pp + 4;
                    _predDataSize = total - predBase - 4;
                }
            }
        }
    }
    return true;
}

bool IME::begin()
{
    if (_loaded)
        return true;

    // Dictionary is memory-mapped in flash - no SD, no file handle.
    _blob = _binary_IME_ime_table_bin_start;
    _blobSize = (size_t)(_binary_IME_ime_table_bin_end - _binary_IME_ime_table_bin_start);
    if (_blobSize < HEADER_SIZE || !parseHeader(_blob, _blobSize))
    {
        _blob = nullptr;
        return false;
    }
    _loaded = true;
    loadUserDict();
    static const char *NAMES[] = {"Wubi", "Pinyin", "Shuangpin"};
    _log("[IME] ready: %s, %u records, codeLen %d (embedded in flash)\n",
         NAMES[_scheme <= SHUANGPIN ? _scheme : 0], (unsigned)_count, _codeLen);
    return true;
}

void IME::loadUserDict()
{
    _userWords.clear();
    if (!SPIFFS.begin(false)) return; // don't format, just try to mount
    File f = SPIFFS.open("/ime_user.txt", "r");
    if (!f) return;

    // Safety limit: don't read more than 8KB to prevent blocking
    const size_t MAX_READ = 8192;
    size_t bytesRead = 0;

    while (f.available() && bytesRead < MAX_READ) {
        String line = f.readStringUntil('\n');
        line.trim();
        bytesRead += line.length();

        if (line.length() < 3) continue;
        int sp1 = line.indexOf(' ');
        if (sp1 < 1) continue;
        String code = line.substring(0, sp1);
        int sp2 = line.indexOf(' ', sp1 + 1);
        String word;
        int count = 1;
        if (sp2 > sp1) {
            word = line.substring(sp1 + 1, sp2);
            count = line.substring(sp2 + 1).toInt();
            if (count < 1) count = 1;
        } else {
            word = line.substring(sp1 + 1);
        }
        if (code.length() >= 1 && word.length() >= 2)
            _userWords.push_back({code, word, count});
    }
    f.close();
    if (_userWords.size() > 0)
        _log("[IME] loaded %d user words\n", (int)_userWords.size());
}

void IME::saveUserDict()
{
    if (!_userDirty) return;
    if (!SPIFFS.begin(false) && !SPIFFS.begin(true)) return;
    File f = SPIFFS.open("/ime_user.txt", "w");
    if (!f) return;
    for (auto &p : _userWords) {
        f.print(p.code); f.print(" ");
        f.print(p.word); f.print(" ");
        f.println(p.count);
    }
    f.close();
    _userDirty = false;
}

void IME::addUserWord(const String &code, const String &word)
{
    // 支持单字(word.length()==3)和词组(word.length()>3)
    if (word.length() < 3) return;  // UTF-8单字最小3字节
    if (code.length() == 0) return;

    // 检查是否已存在
    for (auto &p : _userWords)
        if (p.code == code && p.word == word) return;

    // 限制最多1000个用户词/字
    if (_userWords.size() >= 1000)
        _userWords.erase(_userWords.begin());

    _userWords.push_back({code, word, 0});
    _userDirty = true;
    saveUserDict();
}

// ---- 两分拆字 (embedded PROGMEM array in lf_data.cpp) --------------------
extern "C" const uint8_t _lf_data[];

void IME::loadLfDict()
{
    if (_lfBlob) return;
    _lfBlob = _lf_data;
    if (memcmp(_lfBlob, "IME3", 4) != 0) { _lfBlob = nullptr; return; }
    _lfCount = (uint32_t)_lfBlob[8] | ((uint32_t)_lfBlob[9] << 8) |
               ((uint32_t)_lfBlob[10] << 16) | ((uint32_t)_lfBlob[11] << 24);
    _lfRecordBase = 12 + INDEX_ENTRIES * 4;
    _lfIndex.resize(INDEX_ENTRIES);
    if (_lfIndex.empty()) { _lfBlob = nullptr; return; } // OOM guard
    for (int k = 0; k < INDEX_ENTRIES; k++) {
        const uint8_t *p = _lfBlob + 12 + k * 4;
        _lfIndex[k] = (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    }
    _log("[IME] liangfen: %u records\n", (unsigned)_lfCount);
}

void IME::searchLfWindow(const char *code, int len, uint32_t &lo, uint32_t &hi)
{
    lo = 0; hi = _lfCount;
    if (_lfIndex.empty() || len < 1) return;
    int c0 = code[0] - 'a'; if (c0 < 0 || c0 >= 26) return;
    if (len == 1) { lo = _lfIndex[c0*26]; hi = _lfIndex[(c0+1)*26]; return; }
    int c1 = code[1] - 'a'; if (c1 < 0 || c1 >= 26) return;
    int k = c0 * 26 + c1;
    lo = _lfIndex[k]; hi = _lfIndex[k + 1];
}

bool IME::readLfCode(uint16_t i, char out[7])
{
    const uint8_t *rec = _lfBlob + _lfRecordBase + i * 10;
    int n = 0;
    for (; n < 6 && rec[n]; n++) out[n] = (char)rec[n];
    out[n] = '\0';
    return true;
}

bool IME::readLfHanzi(uint16_t i, char out[4])
{
    const uint8_t *rec = _lfBlob + _lfRecordBase + i * 10 + 6;
    out[0] = (char)rec[0]; out[1] = (char)rec[1]; out[2] = (char)rec[2]; out[3] = '\0';
    return true;
}

void IME::bumpFrequency(const String &code, const String &word)
{
    // 动态词频调整: 单字和词组都支持
    // 用户选择的字/词会自动增加频次并置顶显示

    // 检查是否已存���于���户词库
    for (auto it = _userWords.begin(); it != _userWords.end(); ++it)
    {
        if (it->code == code && it->word == word)
        {
            it->count++;
            _userDirty = true;
            saveUserDict();
            return;
        }
    }

    // 不存在:添加到用户词库
    // 单字(word.length() == 3 bytes in UTF-8) 和 词组都添加
    if (word.length() >= 3 && code.length() >= 1)
    {
        addUserWord(code, word);
        for (auto &p : _userWords)
            if (p.code == code && p.word == word) {
                p.count++;
                break;
            }
    }
}

// Read the code of record `i` (NUL-terminated) from the flash blob.
bool IME::readCode(uint32_t i, char out[MAX_CODE_LEN + 1])
{
    const uint8_t *rec = _blob + _recordBase + (size_t)i * _recordSize;
    int n = 0;
    for (; n < _codeLen && rec[n]; n++)
        out[n] = (char)rec[n];
    out[n] = '\0';
    return true;
}

bool IME::readHanzi(uint32_t i, char out[HANZI_SIZE + 1])
{
    const uint8_t *rec = _blob + _recordBase + (size_t)i * _recordSize + _codeLen;
    out[0] = (char)rec[0];
    out[1] = (char)rec[1];
    out[2] = (char)rec[2];
    out[3] = '\0';
    return true;
}

void IME::setActive(bool on)
{
    _active = on;
    if (on) loadUserDict(); // re-read user dict each time IME is turned on
    reset();
}

void IME::reset()
{
    // Clear composition state but minimize memory fragmentation
    _code = "";

    // Preserve reasonable capacity to avoid reallocation on next lookup.
    // Only shrink excessively large vectors that accumulated from previous
    // higher candidate limits or edge cases.
    _all.clear();
    if (_all.capacity() > 100)
        _all.shrink_to_fit();      // Release excess capacity

    _page.clear();
    if (_page.capacity() > _pageSize)
        _page.shrink_to_fit();     // Release excess capacity

    _pageStart = 0;
    _prefix = "";
    _remainder = "";
    _lfMode = false;
}

// Narrow to the record window that can match the first one/two typed letters,
// using the RAM prefix index. With no index (legacy WUB1) this is [0,_count).
void IME::searchWindow(const char *code, int len, uint32_t &lo, uint32_t &hi)
{
    lo = 0;
    hi = _count;
    if (_index.empty() || len < 1)
        return;

    int c0 = code[0] - 'a';
    if (c0 < 0 || c0 >= 26)
        return; // out of a-z: keep full range (shouldn't happen)

    if (len == 1)
    {
        // all codes starting with c0: buckets [c0*26 .. (c0+1)*26)
        lo = _index[c0 * 26];
        hi = _index[(c0 + 1) * 26];
        return;
    }

    int c1 = code[1] - 'a';
    if (c1 < 0 || c1 >= 26)
        return;
    int k = c0 * 26 + c1;
    lo = _index[k];
    hi = _index[k + 1]; // sentinel guarantees k+1 <= INDEX_ENTRIES-1
}

void IME::lookup()
{
    _all.clear();
    _pageStart = 0;
    _maxMatchLen = 0;
    if (_prefix.length() == 0) _codeOrig = _code;
    static bool dictLoaded = false;
    if (!dictLoaded) { dictLoaded = true; loadUserDict(); loadLfDict(); }

    if (!_loaded || _code.length() == 0)
    {
        buildPage();
        return;
    }

    const char *q = _code.c_str();
    int qlen = _code.length();

    // 两分 mode: code starting with 'u' → strip and search liangfen
    if (_lfMode && _lfBlob)
    {
        uint32_t llo, lhi;
        searchLfWindow(q, qlen, llo, lhi);
        while (llo < lhi) {
            uint32_t mid = llo + (lhi - llo) / 2;
            char code[7]; if (!readLfCode(mid, code)) break;
            if (strncmp(code, q, qlen) < 0) llo = mid + 1;
            else lhi = mid;
        }
        for (uint32_t i = llo; i < _lfCount && _all.size() < 100; i++) {
            char code[7]; if (!readLfCode(i, code)) break;
            if (strncmp(code, q, qlen) != 0) break;
            char hz[4]; if (!readLfHanzi(i, hz)) break;
            String h(hz);
            bool dup = false;
            for (auto &e : _all) if (e == h) { dup = true; break; }
            if (!dup) _all.push_back(h);
        }
        buildPage();
        return;
    }

    // ========================================
    // 新的5级匹配优先级 (严格按序执行)
    // ========================================

    // 阶段1: 用户词库匹配 (最高优先级)
    // ----------------------------------------
    std::vector< std::pair<int, String> > userFreq;
    for (auto &p : _userWords) {
        if (p.code.length() < qlen) continue;
        if (strncmp(p.code.c_str(), q, qlen) == 0) {
            userFreq.push_back({p.count, p.word});
        }
    }
    // 按频次降序排序
    std::sort(userFreq.begin(), userFreq.end(),
        [](const std::pair<int,String> &a, const std::pair<int,String> &b) {
            return a.first > b.first;
        });
    for (auto &f : userFreq) {
        _all.push_back(f.second);
        if (_all.size() >= 100) break;
    }
    if (_all.size() >= 100) { buildPage(); return; }

    // 阶段2: 精确匹配 (完整拼音)
    // ----------------------------------------
    // 检测输入是否包含元音(区分精确匹配和简拼)
    bool hasVowel = false;
    for (int i = 0; i < qlen; i++) {
        if (strchr("aeiouv", q[i])) { hasVowel = true; break; }
    }

    if (hasVowel) {
        // 精确匹配: 单字
        uint32_t lo, hi;
        searchWindow(q, qlen, lo, hi);
        uint32_t scanEnd = hi;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            char code[7];
            if (!readCode(mid, code)) break;
            if (strncmp(code, q, qlen) < 0) lo = mid + 1;
            else hi = mid;
        }
        for (uint32_t i = lo; i < scanEnd && _all.size() < 100; i++) {
            char code[7];
            if (!readCode(i, code)) break;
            if (strncmp(code, q, qlen) != 0) break;
            char hz[4];
            if (!readHanzi(i, hz)) break;
            String h(hz);
            bool dup = false;
            for (auto &e : _all) if (e == h) { dup = true; break; }
            if (!dup) {
                _all.push_back(h);
                if ((int)strlen(code) > _maxMatchLen) _maxMatchLen = strlen(code);
            }
        }

        // 精确匹配: 词组 (完整拼音匹配)
        if (_wordCount > 0 && _wordData) {
            size_t wlo = 0, whi = _wordDataSize;
            if (qlen >= 2) {
                int k = (q[0] - 'a') * 26 + (q[1] - 'a');
                if (k >= 0 && k < INDEX_ENTRIES) {
                    wlo = _wordIndex[k];
                    whi = (k + 1 < INDEX_ENTRIES) ? _wordIndex[k + 1] : _wordDataSize;
                }
            }
            size_t wpos = wlo;
            int safety = 0;
            while (wpos < whi && _all.size() < 100 && safety++ < 5000) {
                uint8_t cl = _wordData[wpos];
                if (cl == 0 || wpos + 1 + cl > whi) break;
                const char *wc = (const char *)_wordData + wpos + 1;
                wpos += 1 + cl;
                if (wpos >= whi) break;
                uint8_t n = _wordData[wpos++];
                // 精确匹配: code必须完全相等
                if ((int)cl == qlen && strncmp(wc, q, qlen) == 0) {
                    for (uint8_t j = 0; j < n && wpos < whi; j++) {
                        uint8_t wl = _wordData[wpos++];
                        if (wl == 0 || wpos + wl > whi) break;
                        String w;
                        w.concat((const char *)_wordData + wpos, wl);
                        wpos += wl;
                        bool dup = false;
                        for (auto &e : _all) if (e == w) { dup = true; break; }
                        if (!dup) {
                            _all.push_back(w);
                            if ((int)cl > _maxMatchLen) _maxMatchLen = cl;
                        }
                    }
                } else {
                    for (uint8_t j = 0; j < n && wpos < whi; j++) {
                        uint8_t wl = _wordData[wpos++];
                        if (wpos + wl > whi) break;
                        wpos += wl;
                    }
                }
            }
        }
    }
    else {
        // 阶段3: 简拼匹配 (纯辅音输入,无元音)
        // ----------------------------------------
        if (_wordCount > 0 && _wordData) {
            size_t slo = 0, shi = _wordDataSize;
            if (qlen >= 1 && q[0] >= 'a' && q[0] <= 'z') {
                int k = (q[0] - 'a') * 26;
                slo = _wordIndex[k];
                shi = (k + 26 < INDEX_ENTRIES) ? _wordIndex[k + 26] : _wordDataSize;
            }

            size_t spos = slo;
            int safety = 0;
            while (spos < shi && _all.size() < 100 && safety++ < 60000) {
                uint8_t cl = _wordData[spos];
                if (cl == 0 || spos + 1 + cl > shi) break;
                const char *wc = (const char *)_wordData + spos + 1;
                spos += 1 + cl;
                if (spos >= shi) break;
                uint8_t n = _wordData[spos++];

                // 提取简拼声母
                char init[13]; int o = 0;
                for (int i = 0; i < cl && o < 12; ) {
                    if (strchr("aeiouv", wc[i])) {
                        while (i < cl && strchr("aeiouvngr", wc[i]) && o < 12)
                            init[o++] = wc[i++];
                        continue;
                    }
                    if (i+1 < cl && (wc[i]=='z'||wc[i]=='c'||wc[i]=='s') && wc[i+1]=='h') {
                        init[o++] = wc[i];
                        init[o++] = wc[i+1];
                        i += 2;
                    } else {
                        init[o++] = wc[i];
                        i++;
                    }
                    while (i < cl && strchr("aeiouv", wc[i])) i++;
                    if (i < cl && strchr("ngr", wc[i])) {
                        int j = i;
                        while (j < cl && strchr("ngr", wc[j])) j++;
                        if (j >= cl || !strchr("aeiouv", wc[j])) i = j;
                    }
                }
                init[o] = 0;

                // 匹配简拼
                if (o >= qlen && strncmp(init, q, qlen) == 0) {
                    for (uint8_t j = 0; j < n && spos < shi; j++) {
                        uint8_t wl = _wordData[spos++];
                        if (wl == 0 || spos + wl > shi) break;
                        if (_all.size() < 100) {
                            String w;
                            w.concat((const char *)_wordData + spos, wl);
                            bool dup = false;
                            for (auto &e : _all) if (e == w) { dup = true; break; }
                            if (!dup) {
                                _all.push_back(w);
                                if ((int)cl > _maxMatchLen) _maxMatchLen = cl;
                            }
                        }
                        spos += wl;
                    }
                } else {
                    for (uint8_t j = 0; j < n && spos < shi; j++) {
                        uint8_t wl = _wordData[spos++];
                        if (spos + wl > shi) break;
                        spos += wl;
                    }
                }
            }
        }
    }

    // 阶段4: 简拼+后字匹配 (简拼+完整拼音)
    // ----------------------------------------
    // 检测是否为"简拼+后字"模式: 输入以元音结尾,但开头部分是辅音
    // 例如: zhguo, zgua, xguo 等
    bool shorthandTail = false;
    String typedInit;   // 简拼部分
    String typedTail;   // 后字完整拼音

    if (qlen >= 3 && hasVowel) {
        // 从后往前找最后一个音节的开始位置
        int lastSylStart = qlen;
        for (int i = qlen - 1; i >= 1; i--) {
            if (strchr("aeiouv", q[i])) {
                int j = i;
                while (j > 0 && strchr("aeiouv", q[j-1])) j--;
                if (j > 0 && strchr("bcdfghjklmnpqrstwxyz", q[j-1])) {
                    lastSylStart = j;
                    break;
                }
            }
        }

        // 验证前半部分是否为纯辅音(简拼)
        if (lastSylStart >= 2 && lastSylStart < qlen) {
            bool isPureConsonant = true;
            for (int i = 0; i < lastSylStart; i++) {
                if (strchr("aeiouv", q[i])) { isPureConsonant = false; break; }
            }
            if (isPureConsonant) {
                shorthandTail = true;
                typedInit = String(q, lastSylStart);
                typedTail = String(q + lastSylStart, qlen - lastSylStart);
            }
        }
    }

    if (shorthandTail && _wordCount > 0 && _wordData && _all.size() < 100) {
        size_t slo = 0, shi = _wordDataSize;
        if (typedInit.length() >= 1 && typedInit[0] >= 'a' && typedInit[0] <= 'z') {
            int k = (typedInit[0] - 'a') * 26;
            slo = _wordIndex[k];
            shi = (k + 26 < INDEX_ENTRIES) ? _wordIndex[k + 26] : _wordDataSize;
        }

        size_t spos = slo;
        int safety = 0;
        while (spos < shi && _all.size() < 100 && safety++ < 60000) {
            uint8_t cl = _wordData[spos];
            if (cl == 0 || spos + 1 + cl > shi) break;
            const char *wc = (const char *)_wordData + spos + 1;
            spos += 1 + cl;
            if (spos >= shi) break;
            uint8_t n = _wordData[spos++];

            // 提取简拼声母
            char init[13]; int o = 0;
            for (int i = 0; i < cl && o < 12; ) {
                if (strchr("aeiouv", wc[i])) {
                    while (i < cl && strchr("aeiouvngr", wc[i]) && o < 12)
                        init[o++] = wc[i++];
                    continue;
                }
                if (i+1 < cl && (wc[i]=='z'||wc[i]=='c'||wc[i]=='s') && wc[i+1]=='h') {
                    init[o++] = wc[i];
                    init[o++] = wc[i+1];
                    i += 2;
                } else {
                    init[o++] = wc[i];
                    i++;
                }
                while (i < cl && strchr("aeiouv", wc[i])) i++;
                if (i < cl && strchr("ngr", wc[i])) {
                    int j = i;
                    while (j < cl && strchr("ngr", wc[j])) j++;
                    if (j >= cl || !strchr("aeiouv", wc[j])) i = j;
                }
            }
            init[o] = 0;

            // 检查简拼是否匹配
            if (o < (int)typedInit.length() || strncmp(init, typedInit.c_str(), typedInit.length()) != 0) {
                for (uint8_t j = 0; j < n && spos < shi; j++) {
                    uint8_t wl = _wordData[spos++];
                    if (spos + wl > shi) break;
                    spos += wl;
                }
                continue;
            }

            // 提取最后一个音节
            int lastSylStart = cl;
            for (int i = cl - 1; i >= 0; i--) {
                if (strchr("aeiouv", wc[i])) {
                    int j = i;
                    while (j > 0 && strchr("aeiouv", wc[j-1])) j--;
                    if (j > 0) { lastSylStart = j; break; }
                }
            }

            // 检查后字是否匹配
            const char *candTailStart = wc + lastSylStart;
            int candTailLen = cl - lastSylStart;
            bool tailMatch = false;
            if (candTailLen >= (int)typedTail.length()) {
                tailMatch = (strncmp(candTailStart, typedTail.c_str(), typedTail.length()) == 0);
            } else {
                tailMatch = (strncmp(typedTail.c_str(), candTailStart, candTailLen) == 0);
            }

            if (!tailMatch) {
                for (uint8_t j = 0; j < n && spos < shi; j++) {
                    uint8_t wl = _wordData[spos++];
                    if (spos + wl > shi) break;
                    spos += wl;
                }
                continue;
            }

            // 匹配成功,添加候选词
            for (uint8_t j = 0; j < n && spos < shi; j++) {
                uint8_t wl = _wordData[spos++];
                if (wl == 0 || spos + wl > shi) break;
                if (_all.size() < 100) {
                    String w;
                    w.concat((const char *)_wordData + spos, wl);
                    bool dup = false;
                    for (auto &e : _all) if (e == w) { dup = true; break; }
                    if (!dup) {
                        _all.push_back(w);
                        if ((int)cl > _maxMatchLen) _maxMatchLen = cl;
                    }
                }
                spos += wl;
            }
        }
    }

    // 阶段5: 逐字匹配 (前缀匹配,最后兜底)
    // ----------------------------------------
    _partialStart = _all.size();
    _remainder = "";
    if (qlen > 1 && _all.size() < 100) {
        uint32_t zlo, zhi;
        int maxTry = qlen - 1;
        if (_maxMatchLen > 0 && _maxMatchLen < maxTry)
            maxTry = _maxMatchLen - 1;

        for (int tryLen = maxTry; tryLen >= 1 && _all.size() < 100; tryLen--) {
            searchWindow(q, tryLen, zlo, zhi);
            uint32_t sEnd = zhi;
            int bcount = 0;
            while (zlo < zhi && bcount++ < 200) {
                uint32_t mid = zlo + (zhi - zlo) / 2;
                char code[7]; if (!readCode(mid, code)) break;
                if (strncmp(code, q, tryLen) < 0) zlo = mid + 1;
                else zhi = mid;
            }
            for (uint32_t i = zlo; i < sEnd && _all.size() < 100; i++) {
                char code[7]; if (!readCode(i, code)) break;
                if (strncmp(code, q, tryLen) != 0) break;
                char hz[4]; if (!readHanzi(i, hz)) break;
                String h(hz);
                bool dup = false;
                for (auto &e : _all) if (e == h) { dup = true; break; }
                if (!dup) _all.push_back(h);
            }
            if (_all.size() > (size_t)_partialStart) {
                _remainder = _code.substring(tryLen);
                break;
            }
        }
    }

    buildPage();
}

void IME::beginPredict(const String &text)
{
    if (!_predData || _predDataSize < 3)
        return;
    reset();
    _predChar = text;
    _predicting = true;

    // Binary-search the prediction table for `text`'s first character.
    const char *first = text.c_str();
    const uint8_t *p = _predData;
    size_t remaining = _predDataSize;
    while (remaining >= 3)
    {
        // Jump to middle
        size_t mid_off = 0;
        size_t count = 0;
        const uint8_t *scan = _predData;
        while (scan < _predData + _predDataSize)
        {
            // Each record: char_utf8[3] + entryCount[1] + entries
            int charLen = 0;
            if ((*scan & 0xE0) == 0xC0) charLen = 2;
            else if ((*scan & 0xF0) == 0xE0) charLen = 3;
            else charLen = 1;
            if (charLen < 1 || scan + charLen + 1 > _predData + _predDataSize) break;
            if (++count == 0) break; // overflow guard
            size_t ec = scan[charLen]; // entry count
            scan += charLen + 1;
            for (size_t j = 0; j < ec && scan < _predData + _predDataSize; j++)
            {
                if (scan + 1 > _predData + _predDataSize) break;
                size_t wl = *scan;
                scan += 1 + wl;
            }
        }
        // For simplicity, linear scan the prediction table (usually <4000 entries).
        // Binary search is overkill and complex with variable-length records.
        break;
    }

    // Linear scan for the matching character
    p = _predData;
    while (p + 3 <= _predData + _predDataSize)
    {
        int charLen = 0;
        if ((*p & 0xE0) == 0xC0) charLen = 2;
        else if ((*p & 0xF0) == 0xE0) charLen = 3;
        else charLen = 1;
        if (charLen < 1 || p + charLen + 1 > _predData + _predDataSize) break;

        if (charLen == (int)text.length() && memcmp(p, first, charLen) == 0)
        {
            // Found: load predictions
            p += charLen;
            uint8_t n = *p++;
            for (uint8_t j = 0; j < n && p < _predData + _predDataSize; j++)
            {
                uint8_t wl = *p++;
                if (wl == 0 || p + wl > _predData + _predDataSize) break;
                String word;
                word.concat((const char *)p, wl);
                p += wl;
                _all.push_back(word);
            }
            buildPage();
            return;
        }
        // skip this record
        uint8_t n = p[charLen];
        p += charLen + 1;
        for (size_t j = 0; j < n && p < _predData + _predDataSize; j++)
        {
            if (p + 1 > _predData + _predDataSize) break;
            uint8_t wl = *p;
            p += 1 + wl;
        }
    }
}

void IME::buildPage()
{
    _page.clear();
    for (int i = _pageStart; i < (int)_all.size() && (int)_page.size() < _pageSize; i++)
        _page.push_back(_all[i]);
}

bool IME::commit(int idx, String &out)
{
    if (idx < 0 || idx >= (int)_page.size())
        return false;
    out = _page[idx];

    // 逐字 continuation: selected hanzi replaces the matched pinyin in
    // the code area, remaining pinyin is processed next.  e.g. type
    // "zhongmeizhou" → select "中" → display "中meizhou" → select "美"
    // → display "中美zhou" → select "州" → commit "中美州".
    // Partial: selected candidate came from 逐字 matching (at or after
    // _partialStart). When there are zero word matches _partialStart==0
    // yet ALL candidates are partial – still allow continuation.
    // _partialStart is an index into _all; convert to page-relative
    int partialRel = _partialStart - _pageStart;
    bool partial = (_remainder.length() > 0 && idx >= partialRel);
    // Only continue if typed code genuinely exceeds matched code length.
    // Guard against corrupted _maxMatchLen with sanity bounds check.
    bool fullWordContinue = (!partial && _maxMatchLen > 0
                             && _maxMatchLen < (int)_code.length()
                             && _maxMatchLen <= 17);

    if (partial || fullWordContinue)
    {
        if (!partial)
            _remainder = _code.substring(_maxMatchLen);
        if (_remainder.length() == 0 || _remainder.length() >= (int)_code.length())
        {
            _prefix = "";
            _remainder = "";
            reset();
            return true;
        }
        _prefix += out;
        _code = _remainder;
        _remainder = "";
        _partialStart = 0;
        _maxMatchLen = 0;
        out = "";  // Don't emit to editor until fully composed
        lookup();
        return false;
    }
    // All pinyin resolved: emit the full composed word
    if (_prefix.length() > 0)
    {
        _prefix += out;
        addUserWord(_codeOrig, _prefix);
        bumpFrequency(_codeOrig, _prefix);
        out = _prefix;
    }
    else
    {
        // Single-step commit: bump frequency for the selected word
        bumpFrequency(_code, out);
    }
    _prefix = "";
    _codeOrig = "";
    reset();
    return true;
}

bool IME::handleKey(int key, String &out)
{
    if (!_active)
        return false;

    // ---- prediction mode (联想) -----------------------------------------
    if (_predicting)
    {
        // letters: end prediction, start normal typing
        if ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z'))
        {
            _predicting = false;
            _code = (char)tolower(key);
            lookup();
            return true;
        }
        // digits: commit the predicted word
        if (key >= '1' && key <= '9')
        {
            int idx = key - '1';
            if (idx < (int)_page.size())
            {
                out = _page[idx];
                _predicting = false;
                return true;
            }
            return true;
        }
        // space commits first prediction
        if (key == ' ')
        {
            if (_page.size() > 0)
            {
                out = _page[0];
                _predicting = false;
            }
            return true;
        }
        // paging keys work for predictions too
        if (key == '-' || key == ';' || key == ',')
        {
            if (_pageStart - _pageSize >= 0)
            {
                _pageStart -= _pageSize;
                buildPage();
            }
            return true;
        }
        if (key == '=' || key == '\'' || key == '.')
        {
            if (_pageStart + _pageSize < (int)_all.size())
            {
                _pageStart += _pageSize;
                buildPage();
            }
            return true;
        }
        // backspace / ESC / enter: cancel prediction
        if (key == '\b' || key == 27 || key == '\n')
        {
            _predicting = false;
            return true;
        }
        // any other key: cancel prediction, let through
        _predicting = false;
        return false;
    }

    // letters a-z (and A-Z) build the code (cap depends on the scheme)
    if ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z'))
    {
        char c = (char)tolower(key);
        // 两分拆字: first letter 'u' activates 两分 mode
        // 两分拆字: first 'u' activates liangfen mode
        if (_code.length() == 0 && c == 'u')
        {
            loadLfDict();
            if (_lfBlob) { _lfMode = true; return true; }
        }
        if ((int)_code.length() < _maxCode)
        {
            _code += c;
            lookup();
        }
        return true;
    }

    // nothing in progress: only the explicit control keys below are ours,
    // everything else (including normal punctuation) goes to the editor.
    if (_code.length() == 0)
        return false;

    // digits select a candidate on the current page (1..9)
    if (key >= '1' && key <= '9')
    {
        if (commit(key - '1', out))
            return true;
        return true; // consumed even if out of range
    }

    // space commits the first candidate
    if (key == ' ')
    {
        if (_page.size() > 0)
            commit(0, out);
        else
            reset(); // no match - just drop the bad code
        return true;
    }

    // Enter: commit the typed letters as-is (raw pinyin), do not convert.
    if (key == '\n')
    {
        out = _code;   // emit the raw typed letters
        reset();
        return true;
    }

    // backspace removes the last code letter
    if (key == '\b')
    {
        if (_code.length() > 0)
        {
            _code.remove(_code.length() - 1);
            if (_code.length() == 0)
                reset();
            else
                lookup();
        }
        return true;
    }

    // ESC cancels the whole composition
    if (key == 27)
    {
        reset();
        return true;
    }

    // paging: '-' / ';' / ',' previous page, '=' / '\'' / '.' next page.
    // '-' and '=' are the handy ones for long lists (e.g. Pinyin). Ctrl-'-'/'='
    // (font size) is intercepted earlier by the keyboard layer, so a plain
    // '-'/'=' only reaches here while composing.
    if (key == '-' || key == ';' || key == ',')
    {
        if (_pageStart - _pageSize >= 0)
        {
            _pageStart -= _pageSize;
            buildPage();
        }
        return true;
    }
    if (key == '=' || key == '\'' || key == '.')
    {
        if (_pageStart + _pageSize < (int)_all.size())
        {
            _pageStart += _pageSize;
            buildPage();
        }
        return true;
    }

    // Any other key while composing (e.g. punctuation): commit the best
    // candidate and consume the key. The key itself is dropped rather than
    // forwarded, which keeps the behaviour predictable - the user can press the
    // punctuation again after the hanzi lands.
    if (_page.size() > 0)
    {
        commit(0, out);
        return true;
    }

    // Unmatched code + unknown key: drop the dead code, consume the key.
    reset();
    return true;
}
