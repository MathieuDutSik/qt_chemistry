#include "ui/HtmlDelegate.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QPainter>
#include <QTextDocument>

namespace qtchem {

void HtmlDelegate::paint(QPainter* painter,
                         const QStyleOptionViewItem& option,
                         const QModelIndex& index) const {
  QStyleOptionViewItem opt = option;
  initStyleOption(&opt, index);

  QTextDocument doc;
  doc.setDocumentMargin(0);
  doc.setDefaultFont(opt.font);
  doc.setHtml(opt.text);

  // Let QStyle draw the background + selection + focus rect, but no text.
  const QString text = opt.text;
  opt.text.clear();
  QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
  style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

  QAbstractTextDocumentLayout::PaintContext ctx;
  ctx.palette = opt.palette;
  if (opt.state & QStyle::State_Selected) {
    ctx.palette.setColor(
        QPalette::Text,
        opt.palette.color(QPalette::Active, QPalette::HighlightedText));
  }

  QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt,
                                         opt.widget);
  doc.setTextWidth(textRect.width());
  painter->save();
  painter->translate(textRect.topLeft());
  painter->setClipRect(textRect.translated(-textRect.topLeft()));
  doc.documentLayout()->draw(painter, ctx);
  painter->restore();
}

QSize HtmlDelegate::sizeHint(const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
  QStyleOptionViewItem opt = option;
  initStyleOption(&opt, index);
  QTextDocument doc;
  doc.setDocumentMargin(0);
  doc.setDefaultFont(opt.font);
  doc.setHtml(opt.text);
  return QSize(static_cast<int>(doc.idealWidth()) + 8,
               static_cast<int>(doc.size().height()) + 4);
}

}
