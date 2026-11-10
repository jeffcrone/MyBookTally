#pragma once

#include <QDialog>
#include <QString>

#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace mbt {

class Database;
enum class LookupKind;

// Add, rename, describe, and delete the records books link to: authors, or
// one of the lookup tables (series, genres, book types, statuses).
//
// One dialog serves all of them because they are edited the same way - a
// list on the left, the selected record's fields on the right. Authors just
// have different fields (bio and birth date instead of a description).
class ManageDialog : public QDialog
{
	Q_OBJECT

public:
	// Manages authors.
	explicit ManageDialog(const Database& database, QWidget* parent = nullptr);
	// Manages one lookup table.
	ManageDialog(const Database& database, LookupKind kind, QWidget* parent = nullptr);

	// True if anything was saved or deleted, so the main window refreshes.
	bool changed() const { return m_changed; }

	// Exposed for the automated tests.
	QListWidget* itemList() const { return m_list; }
	QLineEdit* nameEdit() const { return m_name; }
	QPlainTextEdit* descriptionEdit() const { return m_description; }
	void startNew();
	bool saveCurrent();
	// Deletes the selected record. Asks first unless confirm is false.
	bool deleteCurrent(bool confirm = true);

private:
	void build();
	void reload(const QString& selectId = QString());
	void showSelected();
	bool isAuthors() const { return !m_kind.has_value(); }
	QString singular() const;

	const Database& m_database;
	std::optional<LookupKind> m_kind;
	QString m_currentId;
	bool m_changed = false;

	QListWidget* m_list = nullptr;
	QLineEdit* m_name = nullptr;
	// Description for lookups; bio for authors.
	QPlainTextEdit* m_description = nullptr;
	// Authors only.
	QLineEdit* m_birthDate = nullptr;
	QLabel* m_usage = nullptr;
	QPushButton* m_save = nullptr;
	QPushButton* m_delete = nullptr;
};

} // namespace mbt
