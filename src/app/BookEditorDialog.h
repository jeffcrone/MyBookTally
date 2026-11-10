#pragma once

#include "Entities.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;

namespace mbt {

class Database;
class LinkListEditor;

// Adds a new book or edits an existing one, including its authors, series,
// genres, and cover. Saving happens in accept(): if anything fails the
// dialog stays open with the user's changes intact.
class BookEditorDialog : public QDialog
{
	Q_OBJECT

public:
	// Pass a default-constructed Book (empty id) to add a new one.
	BookEditorDialog(const Database& database, const Book& book, QWidget* parent = nullptr);

	// After the dialog is accepted: the saved book, with its id.
	const Book& savedBook() const { return m_book; }

	void accept() override;

	// Widgets exposed for the automated tests, which fill the form in the
	// way a user would and then press OK.
	QLineEdit* titleEdit() const { return m_title; }
	LinkListEditor* authorsEditor() const { return m_authors; }
	LinkListEditor* genresEditor() const { return m_genres; }
	QLineEdit* isbnEdit() const { return m_isbn; }
	QLineEdit* releaseDateEdit() const { return m_releaseDate; }
	QSpinBox* pagesEdit() const { return m_pages; }
	QComboBox* bookTypeCombo() const { return m_bookType; }
	QCheckBox* privateCheck() const { return m_private; }
	// Adds a series row (name, position text, label) as the Add button does.
	void addSeriesRow(const QString& seriesName, const QString& position, const QString& label);

	// Set by the tests so a bad-checksum ISBN is saved without asking.
	void setConfirmChecksumWarnings(bool confirm) { m_confirmChecksum = confirm; }

private:
	QWidget* buildDetailsPage();
	QWidget* buildSeriesGenresPage();
	QWidget* buildDescriptionCoverPage();
	void loadBook();
	void showCover();
	void chooseCover();
	// Checks the form. Returns an error message, or empty if all is well.
	QString validate();

	const Database& m_database;
	Book m_book;
	QByteArray m_cover;
	bool m_confirmChecksum = false;

	QLineEdit* m_title = nullptr;
	LinkListEditor* m_authors = nullptr;
	QLineEdit* m_releaseDate = nullptr;
	QLineEdit* m_isbn = nullptr;
	QSpinBox* m_pages = nullptr;
	QComboBox* m_bookType = nullptr;
	QComboBox* m_ownership = nullptr;
	QComboBox* m_reading = nullptr;
	QLineEdit* m_location = nullptr;
	QCheckBox* m_private = nullptr;

	QComboBox* m_seriesPicker = nullptr;
	QLineEdit* m_seriesPosition = nullptr;
	QLineEdit* m_seriesLabel = nullptr;
	QTableWidget* m_series = nullptr;
	LinkListEditor* m_genres = nullptr;

	QPlainTextEdit* m_description = nullptr;
	QLabel* m_coverPreview = nullptr;
};

} // namespace mbt
