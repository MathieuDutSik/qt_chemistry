#pragma once

#include <QStyledItemDelegate>

namespace qtchem {

// Editor for the "Units" cell of the input solution table: an editable
// QComboBox populated from qtchem::supportedUnits(). Stays editable so
// uncommon PHREEQC unit strings (e.g. eq/L, meq/kgw) can still be typed.
class UnitDelegate : public QStyledItemDelegate {
  Q_OBJECT
 public:
  explicit UnitDelegate(QObject* parent = nullptr);

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& opt,
                        const QModelIndex& index) const override;
  void setEditorData(QWidget* editor,
                     const QModelIndex& index) const override;
  void setModelData(QWidget* editor, QAbstractItemModel* model,
                    const QModelIndex& index) const override;
};

}
