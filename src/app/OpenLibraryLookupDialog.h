#pragma once

#include "OpenLibrary.h"
#include "OpenLibraryClient.h"

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QStringList>

#include <functional>

class QLabel;
class QPushButton;
class QTableWidget;

namespace mbt {

// Searches Open Library for what the user typed into Add Book, lists the
// books it finds (cover, title, authors, ISBN, date), and once the user
// picks one fetches its full details. Canceling at any point closes the
// dialog and leaves the Add Book form as it was.
//
// The search starts as soon as the dialog is created. Titles appear as soon
// as the search answers; covers fill in afterwards, one per second, because
// every request shares the client's once-a-second limit.
class OpenLibraryLookupDialog : public QDialog
{
	Q_OBJECT

public:
	enum Column { CoverColumn, TitleColumn, AuthorsColumn, IsbnColumn, PublishedColumn, ColumnCount };

	OpenLibraryLookupDialog(OpenLibraryClient& client, const OpenLibraryQuery& query,
		QWidget* parent = nullptr);
	~OpenLibraryLookupDialog() override;

	// After the dialog is accepted: everything found about the chosen book.
	const OpenLibraryBook& chosenBook() const { return m_book; }

	// Shows an error to the user. The default is a warning message box with
	// the full error under "Show Details..."; the tests replace it so they
	// can check the message without a modal box to click through.
	using ErrorReporter = std::function<void(const QString& text, const QString& details)>;
	void setErrorReporter(ErrorReporter reporter) { m_reportError = std::move(reporter); }

	// Exposed for the automated tests.
	QTableWidget* resultsTable() const { return m_results; }
	QPushButton* chooseButton() const { return m_choose; }
	QLabel* statusLabel() const { return m_status; }
	const QList<OpenLibraryMatch>& matches() const { return m_matches; }

	// Uses the selected row, as the "Use This Book" button does.
	void chooseSelected();

	void reject() override;

private:
	void startSearch();
	void searchFinished(const OpenLibraryReply& reply);
	void requestThumbnails();
	void fetchEdition();
	void fetchWork();
	void fetchCover();
	void finishDetails();
	void detailFailed(const QString& what, const QString& message, const QString& details);
	void showError(const QString& text, const QString& details);
	void updateChooseButton();

	// Guarded, because the dialog cancels its requests when it closes and
	// must not touch a client that has already gone.
	QPointer<OpenLibraryClient> m_client;
	OpenLibraryQuery m_query;
	QList<OpenLibraryMatch> m_matches;
	int m_chosenRow = -1;
	OpenLibraryBook m_book;
	// Problems fetching details after a book was picked. They do not stop
	// the lookup - the form is still filled with whatever did arrive - but
	// the user is told about them.
	QStringList m_detailProblems;
	QStringList m_detailErrors;
	bool m_fetchingDetails = false;
	// Owns the thumbnail requests, so picking a book can cancel the covers
	// still waiting and let its own requests go next.
	QObject* m_thumbnailRequests = nullptr;
	ErrorReporter m_reportError;

	QLabel* m_status = nullptr;
	QTableWidget* m_results = nullptr;
	QPushButton* m_choose = nullptr;
};

} // namespace mbt
