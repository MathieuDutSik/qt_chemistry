#include "ui/ChemDelegate.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace qtchem {

namespace {

struct SpeciesParts { std::string body; char sign = 0; std::string magnitude; };
SpeciesParts splitSpecies(const std::string& s) {
  SpeciesParts p;
  if (s.empty()) return p;
  int i = static_cast<int>(s.size());
  const int digits_end = i;
  while (i > 0 && std::isdigit(static_cast<unsigned char>(s[i - 1]))) --i;
  if (i > 0 && (s[i - 1] == '+' || s[i - 1] == '-')) {
    p.sign = s[i - 1];
    p.magnitude = s.substr(i, digits_end - i);
    p.body = s.substr(0, i - 1);
  } else {
    p.body = s;
  }
  return p;
}

bool isNumericToken(const std::string& t) {
  if (t.empty()) return false;
  bool saw_dot = false;
  for (char c : t) {
    if (std::isdigit(static_cast<unsigned char>(c))) continue;
    if (c == '.' && !saw_dot) { saw_dot = true; continue; }
    return false;
  }
  return true;
}

struct Glyph {
  QString text;
  enum Pos { Base, Sub, Sup } pos = Base;
};

std::vector<Glyph> tokenizeSpecies(const std::string& s) {
  std::vector<Glyph> out;
  auto p = splitSpecies(s);
  for (char c : p.body) {
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.')
      out.push_back({QString(QChar::fromLatin1(c)), Glyph::Sub});
    else
      out.push_back({QString(QChar::fromLatin1(c)), Glyph::Base});
  }
  if (p.sign != 0) {
    QString charge;
    for (char c : p.magnitude) charge += QChar::fromLatin1(c);
    charge += QChar::fromLatin1(p.sign);
    out.push_back({charge, Glyph::Sup});
  }
  return out;
}

// Geometry helpers. `painter` may be nullptr: in that case we only measure.
struct Geom {
  QFont base, small, italic;
  QFontMetricsF fm_base, fm_small, fm_italic;
  qreal baseline_y;   // absolute y of baseline within the row
  qreal sub_y;        // baseline of sub glyphs
  qreal sup_y;        // baseline of sup glyphs
  Geom(const QFont& b, qreal origin_y)
      : base(b),
        small(b),
        italic(b),
        fm_base(b),
        fm_small((small.setPointSizeF(b.pointSizeF() * 0.65), small)),
        fm_italic((italic.setItalic(true), italic)) {
    baseline_y = origin_y + fm_base.ascent();
    sub_y      = baseline_y + fm_small.xHeight() * 0.45;
    sup_y      = baseline_y - fm_base.capHeight() * 0.70;
  }
  qreal totalHeight() const {
    const qreal above = fm_base.capHeight() * 0.70 + fm_small.ascent() * 0.5;
    const qreal below = fm_base.descent() + fm_small.xHeight() * 0.45;
    return fm_base.ascent() + below + (above - fm_base.ascent());
  }
};

qreal drawSpeciesGlyphs(QPainter* p, QPointF origin, const Geom& g,
                        const QColor& color,
                        const std::vector<Glyph>& glyphs,
                        bool tagGas, bool tagAq) {
  qreal x = origin.x();
  qreal pending_sub_x = -1, pending_sub_end = -1;

  auto flushPending = [&]() {
    if (pending_sub_end > 0) x = std::max(x, pending_sub_end);
    pending_sub_x = pending_sub_end = -1;
  };

  if (p) p->setPen(color);
  for (const Glyph& glyph : glyphs) {
    if (glyph.pos == Glyph::Base) {
      flushPending();
      if (p) { p->setFont(g.base);
               p->drawText(QPointF(x, g.baseline_y), glyph.text); }
      x += g.fm_base.horizontalAdvance(glyph.text);
    } else if (glyph.pos == Glyph::Sub) {
      flushPending();
      if (p) { p->setFont(g.small);
               p->drawText(QPointF(x, g.sub_y), glyph.text); }
      pending_sub_x = x;
      pending_sub_end = x + g.fm_small.horizontalAdvance(glyph.text);
    } else {  // Sup
      const qreal sup_w = g.fm_small.horizontalAdvance(glyph.text);
      qreal sup_x;
      if (pending_sub_x >= 0) {
        sup_x = pending_sub_x;
        x = std::max(pending_sub_end, sup_x + sup_w);
      } else {
        sup_x = x;
        x += sup_w;
      }
      if (p) { p->setFont(g.small);
               p->drawText(QPointF(sup_x, g.sup_y), glyph.text); }
      pending_sub_x = pending_sub_end = -1;
    }
  }
  flushPending();

  if (tagGas || tagAq) {
    const QString state = tagGas ? QStringLiteral(" (g)") : QStringLiteral(" (aq)");
    if (p) { p->setFont(g.italic);
             p->drawText(QPointF(x, g.baseline_y), state); }
    x += g.fm_italic.horizontalAdvance(state);
  }
  return x - origin.x();
}

// Draws an equation. Returns total advance. If painter is nullptr, only
// measures. For gas phases (`is_gas` true) annotates LHS leader (g) and
// any RHS species matching the LHS body with (aq).
qreal drawReactionImpl(QPainter* p, QPointF origin, const Geom& g,
                       const QColor& color, const std::string& equation,
                       bool is_gas) {
  qreal x = origin.x();
  if (p) p->setPen(color);

  const qreal space_w = g.fm_base.horizontalAdvance(QChar(' '));
  std::string lhs_first_body;
  bool lhs_first_done = false;
  bool seen_equals = false;
  bool first = true;

  size_t pos = 0;
  while (pos < equation.size()) {
    while (pos < equation.size() &&
           std::isspace(static_cast<unsigned char>(equation[pos]))) ++pos;
    if (pos >= equation.size()) break;
    const size_t start = pos;
    while (pos < equation.size() &&
           !std::isspace(static_cast<unsigned char>(equation[pos]))) ++pos;
    const std::string token = equation.substr(start, pos - start);

    if (!first) x += space_w;
    first = false;

    if (token == "+") {
      if (p) { p->setFont(g.base);
               p->drawText(QPointF(x, g.baseline_y), QStringLiteral("+")); }
      x += g.fm_base.horizontalAdvance(QChar('+'));
    } else if (token == "=") {
      seen_equals = true;
      const QString rev = QString(QChar(0x21CC));  // ⇌
      if (p) { p->setFont(g.base);
               p->drawText(QPointF(x, g.baseline_y), rev); }
      x += g.fm_base.horizontalAdvance(rev);
    } else if (isNumericToken(token)) {
      const QString s = QString::fromStdString(token);
      if (p) { p->setFont(g.base);
               p->drawText(QPointF(x, g.baseline_y), s); }
      x += g.fm_base.horizontalAdvance(s);
    } else {
      const auto parts = splitSpecies(token);
      bool tagGas = false, tagAq = false;
      if (is_gas) {
        if (!seen_equals && !lhs_first_done) {
          tagGas = true;
          lhs_first_body = parts.body;
          lhs_first_done = true;
        } else if (seen_equals && parts.body == lhs_first_body) {
          tagAq = true;
        }
      }
      const auto glyphs = tokenizeSpecies(token);
      x += drawSpeciesGlyphs(p, QPointF(x, origin.y()), g, color,
                              glyphs, tagGas, tagAq);
    }
  }
  return x - origin.x();
}

}  // namespace

ChemDelegate::ChemDelegate(Mode mode, QObject* parent)
    : QStyledItemDelegate(parent), mode_(mode) {}

void ChemDelegate::paint(QPainter* painter,
                         const QStyleOptionViewItem& option,
                         const QModelIndex& index) const {
  QStyleOptionViewItem opt = option;
  initStyleOption(&opt, index);

  // Background + selection + focus rect.
  const QString cell_text = opt.text;
  opt.text.clear();
  QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
  style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

  const QRect textRect = style->subElementRect(
      QStyle::SE_ItemViewItemText, &opt, opt.widget);
  const QColor color = opt.palette.color(
      QPalette::Active,
      (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText
                                            : QPalette::Text);

  const std::string s = cell_text.toStdString();
  Geom geom(opt.font, textRect.top());
  painter->save();
  if (mode_ == Mode::Species) {
    const auto glyphs = tokenizeSpecies(s);
    drawSpeciesGlyphs(painter, textRect.topLeft(), geom, color,
                      glyphs, false, false);
  } else {
    const std::string phase =
        index.data(kPhaseNameRole).toString().toStdString();
    const bool is_gas = phase.size() >= 3 &&
                        phase.substr(phase.size() - 3) == "(g)";
    drawReactionImpl(painter, textRect.topLeft(), geom, color, s, is_gas);
  }
  painter->restore();
}

QSize ChemDelegate::sizeHint(const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
  QStyleOptionViewItem opt = option;
  initStyleOption(&opt, index);
  Geom geom(opt.font, 0);
  qreal w = 0;
  const std::string s = opt.text.toStdString();
  if (mode_ == Mode::Species) {
    const auto glyphs = tokenizeSpecies(s);
    w = drawSpeciesGlyphs(nullptr, QPointF(0, 0), geom, QColor(),
                           glyphs, false, false);
  } else {
    const std::string phase =
        index.data(kPhaseNameRole).toString().toStdString();
    const bool is_gas = phase.size() >= 3 &&
                        phase.substr(phase.size() - 3) == "(g)";
    w = drawReactionImpl(nullptr, QPointF(0, 0), geom, QColor(), s, is_gas);
  }
  return QSize(static_cast<int>(w) + 12,
               static_cast<int>(geom.totalHeight()) + 6);
}

}
