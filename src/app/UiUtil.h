#pragma once

#include <QString>

#include <functional>

class QComboBox;
class QIcon;
class QWidget;

namespace mbt {

class Database;
enum class LookupKind;

// Runs action, and if it throws DatabaseError shows the message in a dialog
// over parent. Returns true if action finished without throwing.
//
// Every menu command that touches the database goes through this, so a
// failed query is reported to the user instead of ending the program with
// an uncaught exception.
bool runReportingErrors(QWidget* parent, const QString& title, const std::function<void()>& action);

// Fills combo with "(none)" followed by every row of a lookup table, each
// item carrying the row id as its data, and selects selectedId.
void fillLookupCombo(QComboBox* combo, const Database& database, LookupKind kind,
	const QString& selectedId);

// The id stored on the combo's current item ("" for "(none)").
QString comboId(const QComboBox* combo);

// The main window icon, built from every PNG size in the resources so the
// window manager can pick the sharpest one for each place it is shown.
QIcon appIcon();

} // namespace mbt
