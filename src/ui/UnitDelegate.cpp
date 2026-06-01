#include "ui/UnitDelegate.h"

#include "kernel/UnitConvert.h"

#include <QAbstractItemModel>
#include <QComboBox>

namespace qtchem {

UnitDelegate::UnitDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

QWidget* UnitDelegate::createEditor(QWidget* parent,
                                    const QStyleOptionViewItem&,
                                    const QModelIndex&) const {
  auto* cb = new QComboBox(parent);
  cb->setEditable(true);
  cb->setInsertPolicy(QComboBox::NoInsert);
  for (const auto& u : supportedUnits())
    cb->addItem(QString::fromStdString(u));
  return cb;
}

void UnitDelegate::setEditorData(QWidget* editor,
                                 const QModelIndex& index) const {
  auto* cb = qobject_cast<QComboBox*>(editor);
  if (!cb) return;
  const QString val = index.data(Qt::EditRole).toString();
  const int found = cb->findText(val);
  if (found >= 0) cb->setCurrentIndex(found);
  else cb->setEditText(val);
}

void UnitDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                const QModelIndex& index) const {
  auto* cb = qobject_cast<QComboBox*>(editor);
  if (!cb) return;
  model->setData(index, cb->currentText().trimmed(), Qt::EditRole);
}

}
