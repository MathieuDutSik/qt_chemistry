#pragma once

#include <QString>
#include <string>

namespace qtchem {

// Unicode form — sequential super/subscript glyphs. Cheap, embeds in
// plain text (status bar, tooltips, fallback cells).
QString prettifySpecies(const std::string& name);
QString prettifyReaction(const std::string& equation);

// HTML form — uses <sub>/<sup>, so a subscript stoichiometry and a
// superscript charge can share a column (proper chemistry typography).
// Render with HtmlDelegate in table cells.
QString htmlPrettifySpecies(const std::string& name);
QString htmlPrettifyReaction(const std::string& equation);

// Like htmlPrettifyReaction, but if `phase_name` ends with "(g)" the
// leading LHS species is tagged "(g)" and any matching RHS species
// gets "(aq)". For solids no annotation is added (dissolution to ions
// is self-evident).
QString htmlPrettifyPhaseReaction(const std::string& phase_name,
                                  const std::string& equation);

}
