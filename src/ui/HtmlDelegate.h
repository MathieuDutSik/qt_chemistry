#pragma once

#include <QStyledItemDelegate>

namespace qtchem {

// Renders the item's display text as HTML (subset supported by QTextDocument).
// Selection highlight is preserved by drawing the standard control first,
// then painting the HTML over the text rect with the appropriate palette.
class HtmlDelegate : public QStyledItemDelegate {
  Q_OBJECT
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
};

}
