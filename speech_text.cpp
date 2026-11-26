#include "speech_text.h"

#include <QRegularExpression>
#include <QTextBoundaryFinder>

#include <cmath>

namespace {
constexpr double basePauseMs = 380.0;
constexpr double primaryMsPerUnit = 115.0;
constexpr double taperMsPerUnit = 55.0;
constexpr double taperThresholdUnits = 18.0;
constexpr int minPauseMs = 450;
constexpr int maxPauseMs = 3600;

struct AsideScan {
    qsizetype end = 0;
    bool closed = false;
};

bool openingParenthesis(QChar c)
{
    return c == u'(' || c == u'（';
}

bool closingParenthesis(QChar c)
{
    return c == u')' || c == u'）';
}

AsideScan scanAside(QStringView source, qsizetype start)
{
    qsizetype depth = 1;
    for (auto i = start + 1; i < source.size(); ++i) {
        const auto c = source.at(i);
        if (openingParenthesis(c)) {
            ++depth;
        } else if (closingParenthesis(c) && --depth == 0) {
            return {i + 1, true};
        }
    }
    return {source.size(), false};
}

bool isCjkCharacter(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x4E00 && u <= 0x9FFF)    // CJK Unified Ideographs
        || (u >= 0x3400 && u <= 0x4DBF)    // CJK Extension A
        || (u >= 0x3040 && u <= 0x30FF)    // Hiragana & Katakana
        || (u >= 0xAC00 && u <= 0xD7AF);   // Hangul Syllables
}
}

int asidePauseDurationMs(QStringView innerText)
{
    double units = 0.0;
    bool hasReadableContent = false;
    static const QString clausePunctuation = QStringLiteral("，、,;：:…·—-");
    static const QString sentencePunctuation = QStringLiteral("。！？!?\n");

    for (const QChar c : innerText) {
        if (c == u'\n') {
            units += 1.2;
            continue;
        }
        if (c.isSpace() || openingParenthesis(c) || closingParenthesis(c)
            || QStringLiteral("*`#[]>").contains(c)) {
            continue;
        }
        if (isCjkCharacter(c)) {
            units += 1.0;
            hasReadableContent = true;
        } else if (c.isLetterOrNumber()) {
            units += 0.42;
            hasReadableContent = true;
        } else if (sentencePunctuation.contains(c)) {
            units += 1.2;
        } else if (clausePunctuation.contains(c)) {
            units += 0.6;
        }
    }

    if (!hasReadableContent) {
        return 0;
    }

    const double primary = qMin(units, taperThresholdUnits);
    const double overflow = qMax(0.0, units - taperThresholdUnits);
    const int rawMs = static_cast<int>(std::lround(basePauseMs + primary * primaryMsPerUnit + overflow * taperMsPerUnit));
    return qBound(minPauseMs, rawMs, maxPauseMs);
}

SpeechText textForSpeech(QStringView source, bool finished)
{
    static const QRegularExpression omitted(QStringLiteral(
        R"(https?://[^\s（）()】。！？，、]+|:[a-zA-Z0-9_+-]{2,32}:)"
        R"(|¯\\_\([^\n()]*\)_/¯|ʕ[^\nʔ]{1,32}ʔ)"
        R"(|[ヽヾノ乁╰╭ლ٩ง]{1,3}[(（][^\n()（）]{0,32}[)）][ﾉノ丿/シゞ╯╮づっლ۶و]*)"
        R"(|[(（][^\n()（）]{1,32}[)）][ﾉノ丿╯╮づっლ۶و]{1,3})"
        R"(|(?<![A-Za-z0-9])(?:[TtQqOoUu][_.][TtQqOoUu]|[TQ]A[TQ]|[xX][dD]|orz)(?![A-Za-z0-9]))"
        R"(|[\^><ಠಥ눈][_.][\^><ಠಥ눈]|[<>]{2,}|<3(?![0-9]))"
        R"(|[;:=][\-^']?(?:[)/\\*]|[DPOp](?![A-Za-z0-9])|\((?![\p{L}\p{N}])))"));
    static const QRegularExpression emoji(QStringLiteral(R"([\p{Extended_Pictographic}\p{Regional_Indicator}\p{Emoji_Modifier}\x{20E3}\x{FE0F}])"));
    static const QString formatting = QStringLiteral("*`#[]>");
    SpeechText result;
    result.text.reserve(source.size());
    result.sourceEnds.reserve(source.size());
    if (source.isEmpty()) {
        return result;
    }
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, source.data(), source.size());
    auto omissions = omitted.globalMatchView(source);
    auto nextOmission = [&] { return omissions.hasNext() ? omissions.next() : QRegularExpressionMatch{}; };
    auto omit = nextOmission();
    for (qsizetype i = 0; i < source.size();) {
        while (omit.hasMatch() && omit.capturedStart() < i) {
            omit = nextOmission();
        }
        if (omit.hasMatch() && omit.capturedStart() == i) {
            i = omit.capturedEnd();
            continue;
        }
        if (openingParenthesis(source.at(i))) {
            const auto aside = scanAside(source, i);
            const int pauseMs = aside.closed && aside.end > i + 2
                ? asidePauseDurationMs(source.mid(i + 1, aside.end - i - 2))
                : 0;
            result.asides.append({i, aside.end, result.text.size(), pauseMs, aside.closed});
            i = aside.end;
            continue;
        }
        graphemes.setPosition(i);
        const auto end = graphemes.toNextBoundary();
        const auto glyph = source.mid(i, end - i);
        const auto first = source.at(i);
        // A partial last cluster may acquire an emoji selector or a combining mark in the next chunk.
        if (!finished && end == source.size() && !first.isSpace()
            && !QStringLiteral("。！？!?，、；,").contains(first)) {
            break;
        }
        const bool redundantSpace = first.isSpace() && first != u'\n'
            && (result.text.isEmpty() || result.text.back().isSpace());
        if (!redundantSpace && !formatting.contains(first) && !emoji.matchView(glyph).hasMatch()) {
            result.text.append(glyph);
            for (auto offset = i; offset < end; ++offset) {
                result.sourceEnds.append(offset + 1);
            }
        }
        i = end;
    }
    return result;
}
