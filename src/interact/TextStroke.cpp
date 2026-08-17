#include "interact/TextStroke.h"

namespace cadgeom::interact {
namespace {

/// 字形数据的两个哨兵。坐标本身都在 0..6 里，负数因此可以自由地当控制码用。
constexpr signed char kSep = -1;  ///< 断笔：抬笔，另起一条折线。
constexpr signed char kEnd = -2;  ///< 字形结束。

/// 大写高度，格子单位。归一化除以它，所以输出里 cap height 正好是 1.0 em。
constexpr double kCapHeight = 6.0;
/// 字宽 4 格 + 1 格字距。笔画字体本来就方，这个比例和图纸上的字一致。
constexpr double kAdvance = 5.0 / kCapHeight;

// ---------------------------------------------------------------------------
// 字形表。每个字形是若干条折线，写成 x,y 对，kSep 断笔，kEnd 收尾。
// 格子：x ∈ [0,4]，y ∈ [0,6]，基线在 y = 0。
// ---------------------------------------------------------------------------

#define G(name) const signed char name[]

G(gA) = {0,0, 2,6, 4,0, kSep, 1,2, 3,2, kEnd};
G(gB) = {0,0, 0,6, 3,6, 4,5, 3,3, 0,3, kSep, 3,3, 4,2, 4,1, 3,0, 0,0, kEnd};
G(gC) = {4,5, 3,6, 1,6, 0,5, 0,1, 1,0, 3,0, 4,1, kEnd};
G(gD) = {0,0, 0,6, 2,6, 4,4, 4,2, 2,0, 0,0, kEnd};
G(gE) = {4,6, 0,6, 0,0, 4,0, kSep, 0,3, 3,3, kEnd};
G(gF) = {4,6, 0,6, 0,0, kSep, 0,3, 3,3, kEnd};
G(gG) = {4,5, 3,6, 1,6, 0,5, 0,1, 1,0, 3,0, 4,1, 4,3, 2,3, kEnd};
G(gH) = {0,0, 0,6, kSep, 4,0, 4,6, kSep, 0,3, 4,3, kEnd};
G(gI) = {1,0, 3,0, kSep, 2,0, 2,6, kSep, 1,6, 3,6, kEnd};
G(gJ) = {3,6, 3,1, 2,0, 1,0, 0,1, kEnd};
G(gK) = {0,0, 0,6, kSep, 4,6, 0,3, 4,0, kEnd};
G(gL) = {0,6, 0,0, 4,0, kEnd};
G(gM) = {0,0, 0,6, 2,3, 4,6, 4,0, kEnd};
G(gN) = {0,0, 0,6, 4,0, 4,6, kEnd};
G(gO) = {1,0, 3,0, 4,1, 4,5, 3,6, 1,6, 0,5, 0,1, 1,0, kEnd};
G(gP) = {0,0, 0,6, 3,6, 4,5, 4,4, 3,3, 0,3, kEnd};
G(gQ) = {1,0, 3,0, 4,1, 4,5, 3,6, 1,6, 0,5, 0,1, 1,0, kSep, 2,2, 4,0, kEnd};
G(gR) = {0,0, 0,6, 3,6, 4,5, 4,4, 3,3, 0,3, kSep, 2,3, 4,0, kEnd};
G(gS) = {4,5, 3,6, 1,6, 0,5, 0,4, 1,3, 3,3, 4,2, 4,1, 3,0, 1,0, 0,1, kEnd};
G(gT) = {0,6, 4,6, kSep, 2,6, 2,0, kEnd};
G(gU) = {0,6, 0,1, 1,0, 3,0, 4,1, 4,6, kEnd};
G(gV) = {0,6, 2,0, 4,6, kEnd};
G(gW) = {0,6, 1,0, 2,4, 3,0, 4,6, kEnd};
G(gX) = {0,0, 4,6, kSep, 0,6, 4,0, kEnd};
G(gY) = {0,6, 2,3, 4,6, kSep, 2,3, 2,0, kEnd};
G(gZ) = {0,6, 4,6, 0,0, 4,0, kEnd};

// 0 带一道斜杠，图纸上区分字母 O 的老办法。
G(g0) = {1,0, 3,0, 4,1, 4,5, 3,6, 1,6, 0,5, 0,1, 1,0, kSep, 1,1, 3,5, kEnd};
G(g1) = {1,5, 2,6, 2,0, kSep, 1,0, 3,0, kEnd};
G(g2) = {0,5, 1,6, 3,6, 4,5, 4,4, 0,0, 4,0, kEnd};
G(g3) = {0,5, 1,6, 3,6, 4,5, 4,4, 3,3, 1,3, kSep, 3,3, 4,2, 4,1, 3,0, 1,0, 0,1, kEnd};
G(g4) = {3,0, 3,6, 0,2, 4,2, kEnd};
G(g5) = {4,6, 0,6, 0,4, 3,4, 4,3, 4,1, 3,0, 1,0, 0,1, kEnd};
G(g6) = {4,5, 3,6, 1,6, 0,5, 0,1, 1,0, 3,0, 4,1, 4,2, 3,3, 1,3, 0,2, kEnd};
G(g7) = {0,6, 4,6, 1,0, kEnd};
G(g8) = {1,3, 0,4, 0,5, 1,6, 3,6, 4,5, 4,4, 3,3, 1,3, 0,2, 0,1, 1,0, 3,0, 4,1, 4,2, 3,3, kEnd};
G(g9) = {0,1, 1,0, 3,0, 4,1, 4,5, 3,6, 1,6, 0,5, 0,4, 1,3, 3,3, 4,4, kEnd};

// 句点和逗号画成一格见方的小块 —— 一条零长度的线段是画不出来的。
G(gDot)    = {2,0, 3,0, 3,1, 2,1, 2,0, kEnd};
G(gComma)  = {3,1, 3,0, 2,0, 2,1, 3,1, kSep, 2,0, 1,-1, kEnd};
G(gColon)  = {2,1, 3,1, 3,2, 2,2, 2,1, kSep, 2,4, 3,4, 3,5, 2,5, 2,4, kEnd};
G(gSemi)   = {2,4, 3,4, 3,5, 2,5, 2,4, kSep, 3,2, 3,1, 2,1, 2,2, 3,2, kSep, 2,1, 1,0, kEnd};
G(gMinus)  = {0,3, 4,3, kEnd};
G(gPlus)   = {0,3, 4,3, kSep, 2,1, 2,5, kEnd};
G(gEquals) = {0,2, 4,2, kSep, 0,4, 4,4, kEnd};
G(gSlash)  = {0,0, 4,6, kEnd};
G(gBack)   = {0,6, 4,0, kEnd};
G(gLParen) = {3,6, 1,4, 1,2, 3,0, kEnd};
G(gRParen) = {1,6, 3,4, 3,2, 1,0, kEnd};
G(gLBrack) = {3,6, 1,6, 1,0, 3,0, kEnd};
G(gRBrack) = {1,6, 3,6, 3,0, 1,0, kEnd};
G(gLess)   = {4,6, 0,3, 4,0, kEnd};
G(gGreat)  = {0,6, 4,3, 0,0, kEnd};
G(gStar)   = {2,2, 2,6, kSep, 0,3, 4,5, kSep, 0,5, 4,3, kEnd};
G(gHash)   = {1,0, 1,6, kSep, 3,0, 3,6, kSep, 0,2, 4,2, kSep, 0,4, 4,4, kEnd};
G(gPercent)= {0,0, 4,6, kSep, 0,4, 1,4, 1,5, 0,5, 0,4, kSep, 3,1, 4,1, 4,2, 3,2, 3,1, kEnd};
G(gBang)   = {2,2, 2,6, kSep, 2,0, 3,0, 3,1, 2,1, 2,0, kEnd};
G(gQuery)  = {0,5, 1,6, 3,6, 4,5, 4,4, 2,2, kSep, 2,0, 3,0, 3,1, 2,1, 2,0, kEnd};
G(gApos)   = {2,5, 2,6, kEnd};
G(gQuote)  = {1,5, 1,6, kSep, 3,5, 3,6, kEnd};
G(gUnder)  = {0,0, 4,0, kEnd};
G(gPipe)   = {2,0, 2,6, kEnd};
G(gCaret)  = {0,4, 2,6, 4,4, kEnd};
G(gTilde)  = {0,3, 1,4, 3,2, 4,3, kEnd};

#undef G

/// @return 字形数据，或 null（画不出来的字符，包括空格）。
const signed char* GlyphFor(char c) {
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');  // 小写按大写画。
    }
    switch (c) {
        case 'A': return gA;  case 'B': return gB;  case 'C': return gC;  case 'D': return gD;
        case 'E': return gE;  case 'F': return gF;  case 'G': return gG;  case 'H': return gH;
        case 'I': return gI;  case 'J': return gJ;  case 'K': return gK;  case 'L': return gL;
        case 'M': return gM;  case 'N': return gN;  case 'O': return gO;  case 'P': return gP;
        case 'Q': return gQ;  case 'R': return gR;  case 'S': return gS;  case 'T': return gT;
        case 'U': return gU;  case 'V': return gV;  case 'W': return gW;  case 'X': return gX;
        case 'Y': return gY;  case 'Z': return gZ;

        case '0': return g0;  case '1': return g1;  case '2': return g2;  case '3': return g3;
        case '4': return g4;  case '5': return g5;  case '6': return g6;  case '7': return g7;
        case '8': return g8;  case '9': return g9;

        case '.':  return gDot;     case ',':  return gComma;   case ':': return gColon;
        case ';':  return gSemi;    case '-':  return gMinus;   case '+': return gPlus;
        case '=':  return gEquals;  case '/':  return gSlash;   case '\\': return gBack;
        case '(':  return gLParen;  case ')':  return gRParen;  case '[': return gLBrack;
        case ']':  return gRBrack;  case '<':  return gLess;    case '>': return gGreat;
        case '*':  return gStar;    case '#':  return gHash;    case '%': return gPercent;
        case '!':  return gBang;    case '?':  return gQuery;   case '\'': return gApos;
        case '"':  return gQuote;   case '_':  return gUnder;   case '|': return gPipe;
        case '^':  return gCaret;   case '~':  return gTilde;
        default:   return nullptr;
    }
}

} // namespace

void BuildStrokeText(const char* utf8Text, std::vector<Vec2d>& points,
                     std::vector<StrokeRun>& runs) {
    if (!utf8Text) {
        return;
    }

    double penX = 0.0;
    for (const char* c = utf8Text; *c; ++c) {
        // 非 ASCII 的字节整个跳过：这套字形里没有它们，而把一个多字节序列拆开来
        // 逐字节查表只会画出一串莫名其妙的字母。
        if (static_cast<unsigned char>(*c) >= 0x80) {
            continue;
        }
        if (const signed char* glyph = GlyphFor(*c)) {
            uint32_t first = static_cast<uint32_t>(points.size());
            uint32_t count = 0;
            // 收笔：够两个点才算一条折线，不够就把它扔掉 —— 一个孤立的点在
            // LinePass 里画不出任何东西。
            const auto flush = [&]() {
                if (count >= 2) {
                    runs.push_back(StrokeRun{first, count});
                } else {
                    points.resize(first);
                }
                first = static_cast<uint32_t>(points.size());
                count = 0;
            };

            for (const signed char* p = glyph; *p != kEnd;) {
                if (*p == kSep) {
                    flush();
                    ++p;  // 断笔只占一个字节，坐标占两个。
                    continue;
                }
                points.push_back(Vec2d{penX + static_cast<double>(p[0]) / kCapHeight,
                                       static_cast<double>(p[1]) / kCapHeight});
                ++count;
                p += 2;
            }
            flush();
        }
        penX += kAdvance;
    }
}

double StrokeTextWidth(const char* utf8Text) {
    if (!utf8Text) {
        return 0.0;
    }
    size_t glyphs = 0;
    for (const char* c = utf8Text; *c; ++c) {
        if (static_cast<unsigned char>(*c) < 0x80) {
            ++glyphs;
        }
    }
    return static_cast<double>(glyphs) * kAdvance;
}

} // namespace cadgeom::interact
