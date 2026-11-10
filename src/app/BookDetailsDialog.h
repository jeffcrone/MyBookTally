#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QListWidget;
class QPushButton;
class QTextBrowser;

namespace mbt {

class Database;

// Shows everything about one book - every field, the names of everything it
// links to, its cover - and lets the user rate and review it. Editing the
// book's fields opens BookEditorDialog from here.
class BookDetailsDialog : public QDialog
{
	Q_OBJECT

public:
	BookDetailsDialog(const Database& database, const QString& bookId, QWidget* parent = nullptr);

	// True if anything about the book changed while the dialog was open, so
	// the caller knows to refresh its list.
	bool changed() const { return m_changed; }

	// The details as HTML, as shown in the dialog. Exposed for the tests.
	QString detailsHtml() const;

private:
	void reload();
	void editBook();
	void addReview();
	void editReview();
	void removeReview();
	void addRating();
	void editRating();
	void removeRating();
	void updateButtons();

	const Database& m_database;
	QString m_bookId;
	bool m_changed = false;

	QLabel* m_cover;
	QTextBrowser* m_details;
	QListWidget* m_reviews;
	QListWidget* m_ratings;
	QPushButton* m_editReview;
	QPushButton* m_removeReview;
	QPushButton* m_editRating;
	QPushButton* m_removeRating;
};

} // namespace mbt
