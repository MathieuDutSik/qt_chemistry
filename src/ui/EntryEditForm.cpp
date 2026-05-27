#include "ui/EntryEditForm.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace qtchem {

namespace {

QString fmtOpt(double v, bool present) {
  if (!present) return {};
  return QString::number(v, 'g', 6);
}

QString fmtList(const std::vector<double>& v) {
  QStringList parts;
  parts.reserve(static_cast<int>(v.size()));
  for (double x : v) parts << QString::number(x, 'g', 6);
  return parts.join(QLatin1Char(' '));
}

bool parseDoubleC(const QString& s, double* out) {
  bool ok = false;
  const double v = QLocale::c().toDouble(s.trimmed(), &ok);
  if (ok && out) *out = v;
  return ok;
}

// Best-effort: extract the defined species name from the RHS of an
// aqueous-species equation. Mirrors the kernel's definedSpecies().
QString definedSpeciesQ(const QString& eq) {
  const int eq_pos = eq.indexOf(QLatin1Char('='));
  if (eq_pos < 0) return {};
  const QString rhs = eq.mid(eq_pos + 1).trimmed();
  const auto toks = rhs.split(QRegularExpression(QStringLiteral("\\s+")),
                              Qt::SkipEmptyParts);
  QString last;
  for (const auto& t : toks) {
    if (t == QStringLiteral("+")) continue;
    bool ok = false;
    QLocale::c().toDouble(t, &ok);
    if (ok) continue;
    last = t;
  }
  return last;
}

}  // namespace

EntryEditForm::EntryEditForm(EditableEntry initial, Kind kind,
                              QWidget* parent)
    : QDialog(parent), kind_(kind), entry_(std::move(initial)) {
  switch (kind_) {
    case Kind::Phase:    setWindowTitle(tr("Edit phase")); break;
    case Kind::Aqueous:  setWindowTitle(tr("Edit aqueous species")); break;
    case Kind::Master:   setWindowTitle(tr("Edit master species")); break;
  }

  auto* layout = new QVBoxLayout(this);
  auto* form = new QFormLayout;
  layout->addLayout(form);

  if (kind_ == Kind::Phase) {
    name_edit_ = new QLineEdit(QString::fromStdString(entry_.name));
    name_edit_->setPlaceholderText(tr("e.g. Calcite"));
    form->addRow(tr("Phase name:"), name_edit_);
  } else if (kind_ == Kind::Master) {
    name_edit_ = new QLineEdit(QString::fromStdString(entry_.name));
    name_edit_->setFont(
        QFontDatabase::systemFont(QFontDatabase::FixedFont));
    name_edit_->setPlaceholderText(tr("e.g. Zn+2, CO3-2, H+"));
    form->addRow(tr("Species name:"), name_edit_);
    auto* hint = new QLabel(tr(
        "Master species declare the reference name for an element to "
        "PHREEQC. The reaction is written as <code>name = name</code> "
        "automatically; only the activity-coefficient parameters and "
        "log K (typically 0) are meaningful here. Adding a new master "
        "species also requires an entry in <code>"
        "SOLUTION_MASTER_SPECIES</code> — edit that section in the Raw "
        "text tab."));
    hint->setTextFormat(Qt::RichText);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#666;"));
    form->addRow(QString(), hint);
  } else {
    name_label_ = new QLabel;
    name_label_->setText(entry_.name.empty()
                             ? tr("(derived from equation)")
                             : QString::fromStdString(entry_.name));
    name_label_->setStyleSheet(QStringLiteral("color:#444;"));
    form->addRow(tr("Defined species:"), name_label_);
  }

  if (kind_ != Kind::Master) {
    equation_edit_ = new QLineEdit(QString::fromStdString(entry_.equation));
    equation_edit_->setFont(
        QFontDatabase::systemFont(QFontDatabase::FixedFont));
    equation_edit_->setPlaceholderText(
        kind_ == Kind::Phase ? tr("e.g. CaCO3 = CO3-2 + Ca+2")
                              : tr("e.g. CO3-2 + H+ = HCO3-"));
    form->addRow(tr("Reaction:"), equation_edit_);
    if (kind_ == Kind::Aqueous) {
      connect(equation_edit_, &QLineEdit::textChanged, this,
              [this](const QString& s) {
                const auto d = definedSpeciesQ(s);
                name_label_->setText(d.isEmpty()
                                         ? tr("(derived from equation)")
                                         : d);
              });
    }
  }

  // log K row.
  {
    auto* row = new QHBoxLayout;
    has_log_k_ = new QCheckBox(tr("present"));
    has_log_k_->setChecked(entry_.has_log_k);
    log_k_edit_ = new QLineEdit(fmtOpt(entry_.log_k, entry_.has_log_k));
    log_k_edit_->setEnabled(entry_.has_log_k);
    log_k_edit_->setPlaceholderText(tr("decimal log K at 25 °C"));
    connect(has_log_k_, &QCheckBox::toggled, log_k_edit_, &QWidget::setEnabled);
    row->addWidget(has_log_k_);
    row->addWidget(log_k_edit_, 1);
    auto* w = new QWidget; w->setLayout(row);
    form->addRow(tr("log K:"), w);
  }

  // ΔH row.
  {
    auto* row = new QHBoxLayout;
    has_delta_h_ = new QCheckBox(tr("present"));
    has_delta_h_->setChecked(entry_.has_delta_h);
    delta_h_edit_ = new QLineEdit(fmtOpt(entry_.delta_h, entry_.has_delta_h));
    delta_h_edit_->setEnabled(entry_.has_delta_h);
    delta_h_unit_edit_ = new QLineEdit(QString::fromStdString(
        entry_.delta_h_unit.empty() ? "kJ" : entry_.delta_h_unit));
    delta_h_unit_edit_->setEnabled(entry_.has_delta_h);
    delta_h_unit_edit_->setMaximumWidth(60);
    delta_h_unit_edit_->setPlaceholderText(tr("kJ"));
    delta_h_unit_edit_->setToolTip(tr("Unit: kJ, kcal, J, or cal"));
    connect(has_delta_h_, &QCheckBox::toggled, delta_h_edit_,
            &QWidget::setEnabled);
    connect(has_delta_h_, &QCheckBox::toggled, delta_h_unit_edit_,
            &QWidget::setEnabled);
    row->addWidget(has_delta_h_);
    row->addWidget(delta_h_edit_, 1);
    row->addWidget(new QLabel(tr("unit:")));
    row->addWidget(delta_h_unit_edit_);
    auto* w = new QWidget; w->setLayout(row);
    form->addRow(tr("ΔH:"), w);
  }

  // Analytical expression.
  {
    analytic_edit_ = new QLineEdit(fmtList(entry_.analytic_coeffs));
    analytic_edit_->setFont(
        QFontDatabase::systemFont(QFontDatabase::FixedFont));
    analytic_edit_->setPlaceholderText(
        tr("space-separated A1 A2 A3 A4 A5 [A6]"));
    analytic_edit_->setToolTip(tr(
        "PHREEQC analytic log K(T): log10K = A1 + A2·T + A3/T + A4·log10T + "
        "A5/T² (+ A6·T²). Leave blank to omit."));
    form->addRow(tr("Analytic expr:"), analytic_edit_);
  }

  // -gamma (Truesdell-Jones a0, b).
  {
    auto* row = new QHBoxLayout;
    has_gamma_ = new QCheckBox(tr("present"));
    has_gamma_->setChecked(entry_.has_gamma);
    gamma_a0_edit_ = new QLineEdit(fmtOpt(entry_.gamma_a0, entry_.has_gamma));
    gamma_b_edit_  = new QLineEdit(fmtOpt(entry_.gamma_b,  entry_.has_gamma));
    gamma_a0_edit_->setEnabled(entry_.has_gamma);
    gamma_b_edit_->setEnabled(entry_.has_gamma);
    gamma_a0_edit_->setPlaceholderText(tr("a₀ (Å)"));
    gamma_b_edit_->setPlaceholderText(tr("b"));
    gamma_a0_edit_->setToolTip(tr(
        "Truesdell-Jones ion-size parameter (Ångström)."));
    gamma_b_edit_->setToolTip(tr(
        "Truesdell-Jones empirical correction term."));
    connect(has_gamma_, &QCheckBox::toggled, gamma_a0_edit_,
            &QWidget::setEnabled);
    connect(has_gamma_, &QCheckBox::toggled, gamma_b_edit_,
            &QWidget::setEnabled);
    row->addWidget(has_gamma_);
    row->addWidget(new QLabel(tr("a₀:")));
    row->addWidget(gamma_a0_edit_, 1);
    row->addWidget(new QLabel(tr("b:")));
    row->addWidget(gamma_b_edit_, 1);
    auto* w = new QWidget; w->setLayout(row);
    form->addRow(tr("-gamma:"), w);
  }

  // Verbatim extras.
  if (!entry_.extra_lines.empty()) {
    auto* lbl = new QLabel(tr("Other lines (preserved verbatim, read-only):"));
    layout->addWidget(lbl);
    extras_view_ = new QPlainTextEdit;
    extras_view_->setReadOnly(true);
    extras_view_->setFont(
        QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QString joined;
    for (const auto& l : entry_.extra_lines) {
      joined += QString::fromStdString(l);
      joined += QLatin1Char('\n');
    }
    extras_view_->setPlainText(joined);
    extras_view_->setMaximumHeight(120);
    layout->addWidget(extras_view_);
  }

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, this, &EntryEditForm::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  resize(560, sizeHint().height());
}

void EntryEditForm::onAccept() {
  if (kind_ == Kind::Master) {
    const QString nm = name_edit_->text().trimmed();
    if (nm.isEmpty()) {
      QMessageBox::warning(this, tr("Missing name"),
          tr("Species name cannot be empty."));
      return;
    }
    entry_.name = nm.toStdString();
    entry_.equation = (nm + QStringLiteral(" = ") + nm).toStdString();
  } else {
    const QString eq = equation_edit_->text().trimmed();
    if (!eq.contains(QLatin1Char('='))) {
      QMessageBox::warning(this, tr("Invalid equation"),
          tr("The reaction must contain an '=' sign."));
      return;
    }
    if (kind_ == Kind::Phase) {
      const QString nm = name_edit_->text().trimmed();
      if (nm.isEmpty()) {
        QMessageBox::warning(this, tr("Missing name"),
            tr("Phase name cannot be empty."));
        return;
      }
      entry_.name = nm.toStdString();
    } else {
      const QString d = definedSpeciesQ(eq);
      if (d.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid equation"),
            tr("Cannot derive a species name from the equation. The right-"
               "hand side should contain at least one non-master species."));
        return;
      }
      entry_.name = d.toStdString();
    }
    entry_.equation = eq.toStdString();
  }

  auto readOptionalDouble = [&](QCheckBox* present, QLineEdit* edit,
                                bool* has, double* out, const char* label) {
    if (!present->isChecked()) { *has = false; *out = 0.0; return true; }
    double v;
    if (!parseDoubleC(edit->text(), &v)) {
      QMessageBox::warning(this, tr("Invalid number"),
          tr("Cannot parse %1 as a number.").arg(QString::fromUtf8(label)));
      return false;
    }
    *has = true; *out = v; return true;
  };

  if (!readOptionalDouble(has_log_k_, log_k_edit_,
                          &entry_.has_log_k, &entry_.log_k, "log K"))
    return;

  if (has_delta_h_->isChecked()) {
    double v;
    if (!parseDoubleC(delta_h_edit_->text(), &v)) {
      QMessageBox::warning(this, tr("Invalid number"),
          tr("Cannot parse ΔH as a number."));
      return;
    }
    const QString unit = delta_h_unit_edit_->text().trimmed();
    const QString ul = unit.toLower();
    if (ul != QStringLiteral("kj") && ul != QStringLiteral("kcal") &&
        ul != QStringLiteral("j")  && ul != QStringLiteral("cal")) {
      QMessageBox::warning(this, tr("Invalid unit"),
          tr("ΔH unit must be one of kJ, kcal, J, cal."));
      return;
    }
    entry_.has_delta_h = true;
    entry_.delta_h = v;
    entry_.delta_h_unit = unit.toStdString();
  } else {
    entry_.has_delta_h = false;
    entry_.delta_h = 0.0;
    entry_.delta_h_unit.clear();
  }

  // Analytical coefficients.
  const QString aTxt = analytic_edit_->text().trimmed();
  if (aTxt.isEmpty()) {
    entry_.analytic_coeffs.clear();
  } else {
    const auto toks = aTxt.split(QRegularExpression(QStringLiteral("\\s+")),
                                 Qt::SkipEmptyParts);
    std::vector<double> coeffs;
    coeffs.reserve(toks.size());
    for (const auto& t : toks) {
      double v;
      if (!parseDoubleC(t, &v)) {
        QMessageBox::warning(this, tr("Invalid coefficient"),
            tr("Cannot parse '%1' as a number.").arg(t));
        return;
      }
      coeffs.push_back(v);
    }
    entry_.analytic_coeffs = std::move(coeffs);
  }

  if (has_gamma_->isChecked()) {
    double a0, b;
    if (!parseDoubleC(gamma_a0_edit_->text(), &a0) ||
        !parseDoubleC(gamma_b_edit_->text(),  &b)) {
      QMessageBox::warning(this, tr("Invalid number"),
          tr("Cannot parse -gamma parameters."));
      return;
    }
    entry_.has_gamma = true;
    entry_.gamma_a0 = a0;
    entry_.gamma_b  = b;
  } else {
    entry_.has_gamma = false;
    entry_.gamma_a0 = 0.0;
    entry_.gamma_b  = 0.0;
  }

  accept();
}

}
