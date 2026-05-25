#include "kernel/ChemFormat.h"

#include <cctype>

namespace qtchem {

namespace {

QString subscriptDigit(int d) {
  static const QString kSub = QStringLiteral("₀₁₂₃₄"
                                              "₅₆₇₈₉");
  return (d >= 0 && d <= 9) ? kSub.mid(d, 1) : QString::number(d);
}

QString superscriptDigit(int d) {
  static const QString kSup = QStringLiteral("⁰¹²³⁴"
                                              "⁵⁶⁷⁸⁹");
  return (d >= 0 && d <= 9) ? kSup.mid(d, 1) : QString::number(d);
}

QString renderBodyUnicode(const std::string& body) {
  QString out;
  for (char c : body) {
    if (std::isdigit(static_cast<unsigned char>(c)))
      out += subscriptDigit(c - '0');
    else
      out += QChar::fromLatin1(c);
  }
  return out;
}

QString renderChargeUnicode(char sign, const std::string& magnitude) {
  QString out;
  for (char c : magnitude)
    if (std::isdigit(static_cast<unsigned char>(c)))
      out += superscriptDigit(c - '0');
  out += (sign == '+') ? QStringLiteral("⁺") : QStringLiteral("⁻");
  return out;
}

QString htmlEscape(const std::string& s) {
  QString out;
  for (char c : s) {
    switch (c) {
      case '&': out += QStringLiteral("&amp;"); break;
      case '<': out += QStringLiteral("&lt;");  break;
      case '>': out += QStringLiteral("&gt;");  break;
      default:  out += QChar::fromLatin1(c);
    }
  }
  return out;
}

QString renderBodyHtml(const std::string& body) {
  // Wrap each maximal run of digits (possibly with '.') in <sub>...</sub>.
  QString out;
  size_t i = 0;
  while (i < body.size()) {
    if (std::isdigit(static_cast<unsigned char>(body[i]))) {
      size_t j = i;
      while (j < body.size() &&
             (std::isdigit(static_cast<unsigned char>(body[j])) ||
              body[j] == '.')) ++j;
      out += QStringLiteral("<sub>") +
             htmlEscape(body.substr(i, j - i)) +
             QStringLiteral("</sub>");
      i = j;
    } else {
      out += htmlEscape(std::string(1, body[i]));
      ++i;
    }
  }
  return out;
}

QString renderChargeHtml(char sign, const std::string& magnitude) {
  // Magnitude-first convention (e.g. "2+" / "2-"), wrapped in <sup>.
  QString out = QStringLiteral("<sup>");
  for (char c : magnitude) out += QChar::fromLatin1(c);
  out += QChar::fromLatin1(sign);
  out += QStringLiteral("</sup>");
  return out;
}

// Split a species name into (body, sign, magnitude). Returns sign=0 for
// uncharged species.
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

// Generic equation walker. `render_species` is called for each species
// token, with the token string AND a "side" flag (false=LHS, true=RHS).
template <class Render>
QString walkEquation(const std::string& eq, Render render_species) {
  QString result;
  size_t pos = 0;
  bool first = true;
  bool seen_equals = false;
  while (pos < eq.size()) {
    while (pos < eq.size() &&
           std::isspace(static_cast<unsigned char>(eq[pos]))) ++pos;
    if (pos >= eq.size()) break;
    const size_t start = pos;
    while (pos < eq.size() &&
           !std::isspace(static_cast<unsigned char>(eq[pos]))) ++pos;
    const std::string token = eq.substr(start, pos - start);

    QString rendered;
    if (token == "+")      rendered = QStringLiteral("+");
    else if (token == "=") { rendered = QStringLiteral("⇌"); seen_equals = true; }
    else if (isNumericToken(token))
      rendered = QString::fromStdString(token);
    else
      rendered = render_species(token, seen_equals);

    if (!first) result += QChar(' ');
    result += rendered;
    first = false;
    if (token == "=") seen_equals = true;
  }
  return result;
}

}  // namespace

QString prettifySpecies(const std::string& s) {
  const auto p = splitSpecies(s);
  if (p.sign == 0) return renderBodyUnicode(p.body);
  return renderBodyUnicode(p.body) + renderChargeUnicode(p.sign, p.magnitude);
}

QString prettifyReaction(const std::string& eq) {
  return walkEquation(eq, [](const std::string& t, bool) {
    return prettifySpecies(t);
  });
}

QString htmlPrettifySpecies(const std::string& s) {
  const auto p = splitSpecies(s);
  if (p.sign == 0) return renderBodyHtml(p.body);
  return renderBodyHtml(p.body) + renderChargeHtml(p.sign, p.magnitude);
}

QString htmlPrettifyReaction(const std::string& eq) {
  return walkEquation(eq, [](const std::string& t, bool) {
    return htmlPrettifySpecies(t);
  });
}

QString htmlPrettifyPhaseReaction(const std::string& phase_name,
                                  const std::string& equation) {
  const bool is_gas =
      phase_name.size() >= 3 &&
      phase_name.substr(phase_name.size() - 3) == "(g)";
  if (!is_gas) return htmlPrettifyReaction(equation);

  // For gases: tag the first LHS species "(g)", and any RHS species
  // whose chemical body matches the LHS body "(aq)".
  std::string lhs_first_body;
  bool lhs_first_done = false;
  return walkEquation(
      equation, [&](const std::string& token, bool on_rhs) -> QString {
        const auto p = splitSpecies(token);
        QString s = htmlPrettifySpecies(token);
        if (!on_rhs && !lhs_first_done) {
          lhs_first_body = p.body;
          lhs_first_done = true;
          s += QStringLiteral(" <i>(g)</i>");
        } else if (on_rhs && p.body == lhs_first_body) {
          s += QStringLiteral(" <i>(aq)</i>");
        }
        return s;
      });
}

}
