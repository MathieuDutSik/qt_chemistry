#pragma once

#include <QStyledItemDelegate>

namespace qtchem {

// Renders chemical species or reactions by direct QPainter drawing, so a
// subscript stoichiometry and a superscript charge share the same x
// position (proper chemistry typography). For "Reaction" mode the cell may
// carry the phase name in kPhaseNameRole; when it ends with "(g)" the
// leading LHS species gets "(g)" and matching RHS species "(aq)".
class ChemDelegate : public QStyledItemDelegate {
  Q_OBJECT
 public:
  enum class Mode { Species, Reaction };
  static constexpr int kPhaseNameRole = Qt::UserRole + 1;

  ChemDelegate(Mode mode, QObject* parent = nullptr);

  void paint(QPainter*, const QStyleOptionViewItem&,
             const QModelIndex&) const override;
  QSize sizeHint(const QStyleOptionViewItem&,
                 const QModelIndex&) const override;

 private:
  Mode mode_;
};

}
