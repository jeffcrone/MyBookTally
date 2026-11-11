#pragma once

#include "Entities.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace mbt {

class Database;
class LinkListEditor;
class OpenLibraryClient;
struct OpenLibraryBook;
struct OpenLibraryQuery;

// Adds a new book or edits an existing one, including its authors, series,
// genres, and cover. Saving happens in accept(): if anything fails the
// dialog stays open with the user's changes intact.
class BookEditorDialog : public QDialog
{
	Q_OBJECT

public:
	// Pass a default-constructed Book (empty id) to add a new one.
	//
	// When adding a book and openLibrary is given, the dialog has a "Look Up
	// on Open Library" button that fills the form from Open Library.
	BookEditorDialog(const Database& database, const Book& book, QWidget* parent = nullptr,
		OpenLibraryClient* openLibrary = nullptr);

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

	QPlainTextEdit* descriptionEdit() const { return m_description; }
	// The cover as it would be saved; empty for none.
	const QByteArray& coverImage() const { return m_cover; }
	// Null unless the dialog offers the Open Library lookup.
	QPushButton* lookupButton() const { return m_lookup; }

	// What a lookup would search for: the title, the first author, and the
	// ISBN as currently typed into the form.
	OpenLibraryQuery lookupQuery() const;
	// Asks Open Library about the book, lets the user pick a result, and
	// fills the form with it - what the lookup button does.
	void lookUpOnOpenLibrary();
	// Fills the form from a lookup result. Fields Open Library said nothing
	// about keep what the user typed; ownership, reading status, location,
	// series and the private flag are never touched, since they are about
	// the user's copy and not the book.
	void applyLookup(const OpenLibraryBook& found);

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
	OpenLibraryClient* m_openLibrary = nullptr;
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
	QPushButton* m_lookup = nullptr;
};

} // namespace mbt
