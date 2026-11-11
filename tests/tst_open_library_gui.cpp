// Tests for the Open Library lookup as the user sees it - the button on Add
// Book, the list of results, filling in the form - and for the Settings
// dialog that controls error logging. The lookup runs against a fake server
// on 127.0.0.1 (FakeHttpServer.h), never the real Open Library.

#include "BookEditorDialog.h"
#include "ErrorLog.h"
#include "FakeHttpServer.h"
#include "LinkListEditor.h"
#include "MainWindow.h"
#include "OpenLibrary.h"
#include "OpenLibraryClient.h"
#include "OpenLibraryLookupDialog.h"
#include "OpenLibrarySamples.h"
#include "SettingsDialog.h"
#include "TestSupport.h"
#include "UiUtil.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>

using namespace mbt;

namespace {

// A small real image, so the code that decodes covers has something to
// decode. Portrait, like a cover.
QByteArray coverPng(const QSize& size = QSize(20, 30))
{
	QImage image(size, QImage::Format_RGB32);
	image.fill(Qt::darkGreen);
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}

// An error the dialog would have shown.
struct ShownError
{
	QString text;
	QString details;
};

} // namespace

class TestOpenLibraryGui : public QObject
{
	Q_OBJECT

private:
	// A client pointed at the fake server, with a short interval so the
	// tests do not wait a second between requests.
	std::unique_ptr<OpenLibraryClient> makeClient()
	{
		auto client = std::make_unique<OpenLibraryClient>();
		client->setApiBase(m_server->baseUrl());
		client->setCoversBase(m_server->baseUrl());
		client->setMinimumInterval(20);
		return client;
	}

	// The fake server answering a whole lookup of "The Hobbit".
	void serveHobbit()
	{
		m_server->setJson(QStringLiteral("/search.json"), test::samples::searchHobbit());
		m_server->setJson(QStringLiteral("/books/OL10236414M.json"), test::samples::editionHobbit());
		m_server->setJson(QStringLiteral("/works/OL27482W.json"), test::samples::workHobbit());
		const QByteArray cover = coverPng();
		for (const char* path : {"/b/id/10522108-S.jpg", "/b/id/8406766-S.jpg", "/b/id/10522108-L.jpg"})
			m_server->setResponse(QString::fromLatin1(path),
				test::FakeHttpServer::Response{200, "OK", "image/png", cover, {}});
	}

	std::unique_ptr<QSettings> freshSettings()
	{
		return std::make_unique<QSettings>(
			m_folder.filePath(QStringLiteral("settings-%1.ini").arg(++m_settingsCount)),
			QSettings::IniFormat);
	}

	std::unique_ptr<test::FakeHttpServer> m_server;
	QTemporaryDir m_folder;
	int m_settingsCount = 0;
	QStringList m_console;

private slots:
	void initTestCase()
	{
		QVERIFY(m_folder.isValid());
	}

	void init()
	{
		m_server = std::make_unique<test::FakeHttpServer>();
		QVERIFY(m_server->listen());
		m_console.clear();
		ErrorLog& log = ErrorLog::instance();
		log.setConsoleSink([this](const QString& text) { m_console.append(text); });
		log.setConsoleEnabled(true);
		log.setFileEnabled(false);
		log.setFilePath(m_folder.filePath(QStringLiteral("gui.log")));
	}

	void cleanup()
	{
		m_server.reset();
		ErrorLog::instance().setConsoleSink({});
	}

	// ---- The lookup dialog ----

	void lookupListsResultsWithCovers()
	{
		serveHobbit();
		const auto client = makeClient();
		OpenLibraryLookupDialog dialog(*client,
			OpenLibraryQuery{QStringLiteral("The Hobbit"), QStringLiteral("Tolkien"), QString()});
		QVERIFY(dialog.statusLabel()->text().contains(QStringLiteral("Searching")));
		QVERIFY(!dialog.chooseButton()->isEnabled());

		QTableWidget* table = dialog.resultsTable();
		QTRY_COMPARE(table->rowCount(), 2);
		QCOMPARE(table->item(0, OpenLibraryLookupDialog::TitleColumn)->text(),
			QStringLiteral("The Hobbit: or There and Back Again"));
		QCOMPARE(table->item(0, OpenLibraryLookupDialog::AuthorsColumn)->text(), QStringLiteral("J.R.R. Tolkien"));
		QCOMPARE(table->item(0, OpenLibraryLookupDialog::IsbnColumn)->text(), QStringLiteral("9780261103344"));
		QCOMPARE(table->item(0, OpenLibraryLookupDialog::PublishedColumn)->text(), QStringLiteral("2011"));
		QCOMPARE(table->item(1, OpenLibraryLookupDialog::AuthorsColumn)->text(),
			QStringLiteral("Charles Dixon, Sean Deming, J.R.R. Tolkien"));
		// The first row is selected, ready to choose.
		QCOMPARE(table->currentRow(), 0);
		QVERIFY(dialog.chooseButton()->isEnabled());

		// Both covers arrive as pictures.
		for (int row = 0; row < 2; ++row) {
			QTRY_VERIFY(!table->item(row, OpenLibraryLookupDialog::CoverColumn)
				->data(Qt::DecorationRole).value<QPixmap>().isNull());
		}

		// The search sent what was typed, as separate fields.
		const auto searches = m_server->requestsFor(QStringLiteral("/search.json"));
		QCOMPARE(searches.size(), 1);
		const QUrlQuery query(searches.first().url());
		QCOMPARE(query.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded), QStringLiteral("The Hobbit"));
		QCOMPARE(query.queryItemValue(QStringLiteral("author")), QStringLiteral("Tolkien"));
	}

	void choosingABookFetchesItsDetails()
	{
		serveHobbit();
		const auto client = makeClient();
		OpenLibraryLookupDialog dialog(*client,
			OpenLibraryQuery{QStringLiteral("The Hobbit"), QString(), QString()});
		QList<ShownError> errors;
		dialog.setErrorReporter([&](const QString& text, const QString& details) { errors.append({text, details}); });
		dialog.show();
		QTRY_COMPARE(dialog.resultsTable()->rowCount(), 2);

		dialog.resultsTable()->selectRow(0);
		dialog.chooseButton()->click();
		QVERIFY(!dialog.chooseButton()->isEnabled());
		QTRY_COMPARE(int(dialog.result()), int(QDialog::Accepted));
		QVERIFY(errors.isEmpty());

		const OpenLibraryBook& book = dialog.chosenBook();
		QCOMPARE(book.title, QStringLiteral("The Hobbit: or There and Back Again"));
		QCOMPARE(book.isbn, QStringLiteral("9780261103344"));
		QCOMPARE(book.pageCount, std::optional<int>(310));
		QCOMPARE(book.physicalFormat, QStringLiteral("Paperback"));
		QCOMPARE(book.releaseDate, QStringLiteral("2011"));
		QVERIFY(book.description.startsWith(QStringLiteral("Bilbo")));
		QCOMPARE(book.coverImage, coverPng());
		QCOMPARE(m_server->requestsFor(QStringLiteral("/books/OL10236414M.json")).size(), 1);
		QCOMPARE(m_server->requestsFor(QStringLiteral("/works/OL27482W.json")).size(), 1);
		QCOMPARE(m_server->requestsFor(QStringLiteral("/b/id/10522108-L.jpg")).size(), 1);
	}

	void searchFailureIsReportedAndClosesTheDialog()
	{
		m_server->setError(QStringLiteral("/search.json"), 503, "Service Unavailable", "down for maintenance");
		const auto client = makeClient();
		OpenLibraryLookupDialog dialog(*client, OpenLibraryQuery{QStringLiteral("Dune"), QString(), QString()});
		QList<ShownError> errors;
		dialog.setErrorReporter([&](const QString& text, const QString& details) { errors.append({text, details}); });
		dialog.show();

		QTRY_COMPARE(errors.size(), 1);
		QTRY_COMPARE(int(dialog.result()), int(QDialog::Rejected));
		QVERIFY(!dialog.isVisible());
		// A description in the popup, the full error behind "Show Details".
		QVERIFY(errors.first().text.contains(QStringLiteral("search on Open Library failed")));
		QVERIFY(errors.first().text.contains(QStringLiteral("503")));
		QVERIFY(errors.first().details.contains(QStringLiteral("down for maintenance")));
		// And on the console, because that setting is on.
		QCOMPARE(m_console.size(), 1);
		QVERIFY(m_console.first().contains(QStringLiteral("While: Searching Open Library for title \"Dune\"")));
	}

	void unreadableSearchReplyIsReported()
	{
		m_server->setJson(QStringLiteral("/search.json"), "<html>oops</html>");
		const auto client = makeClient();
		OpenLibraryLookupDialog dialog(*client, OpenLibraryQuery{QStringLiteral("Dune"), QString(), QString()});
		QList<ShownError> errors;
		dialog.setErrorReporter([&](const QString& text, const QString& details) { errors.append({text, details}); });
		QTRY_COMPARE(errors.size(), 1);
		QVERIFY(errors.first().text.contains(QStringLiteral("could not be read")));
		QVERIFY(errors.first().details.contains(QStringLiteral("<html>oops</html>")));
		QCOMPARE(m_console.size(), 1);
		QVERIFY(m_console.first().contains(QStringLiteral("Reading Open Library's search results")));
	}

	void noResultsSaysSo()
	{
		m_server->setJson(QStringLiteral("/search.json"), test::samples::searchNothing());
		const auto client = makeClient();
		OpenLibraryLookupDialog dialog(*client, OpenLibraryQuery{QStringLiteral("Zzyzx"), QString(), QString()});
		QTRY_VERIFY(dialog.statusLabel()->text().contains(QStringLiteral("found no books")));
		QCOMPARE(dialog.resultsTable()->rowCount(), 0);
		QVERIFY(!dialog.chooseButton()->isEnabled());
		// Choosing with nothing listed does nothing.
		dialog.chooseSelected();
		QCOMPARE(int(dialog.result()), int(QDialog::Rejected));
	}

	void missingDetailsStillFillWhatWasFound()
	{
		serveHobbit();
		// The edition record is gone; the work and cover are fine.
		m_server->setError(QStringLiteral("/books/OL10236414M.json"), 404, "Not Found");
		const auto client = makeClient();
		OpenLibraryLookupDialog dialog(*client, OpenLibraryQuery{QStringLiteral("The Hobbit"), QString(), QString()});
		QList<ShownError> errors;
		dialog.setErrorReporter([&](const QString& text, const QString& details) { errors.append({text, details}); });
		QTRY_COMPARE(dialog.resultsTable()->rowCount(), 2);
		dialog.chooseSelected();
		QTRY_COMPARE(int(dialog.result()), int(QDialog::Accepted));

		QCOMPARE(errors.size(), 1);
		QVERIFY(errors.first().text.contains(QStringLiteral("edition details")));
		QVERIFY(errors.first().details.contains(QStringLiteral("404")));
		// From the search row and the work.
		QCOMPARE(dialog.chosenBook().isbn, QStringLiteral("9780261103344"));
		QCOMPARE(dialog.chosenBook().description, QStringLiteral("The Hobbit is a tale of high adventure."));
		QVERIFY(!dialog.chosenBook().pageCount);
	}

	void thumbnailFailureMarksTheRowOnly()
	{
		serveHobbit();
		m_server->setError(QStringLiteral("/b/id/8406766-S.jpg"), 404, "Not Found");
		const auto client = makeClient();
		OpenLibraryLookupDialog dialog(*client, OpenLibraryQuery{QStringLiteral("The Hobbit"), QString(), QString()});
		QList<ShownError> errors;
		dialog.setErrorReporter([&](const QString& text, const QString& details) { errors.append({text, details}); });
		QTRY_COMPARE(dialog.resultsTable()->rowCount(), 2);
		QTRY_COMPARE(dialog.resultsTable()->item(1, OpenLibraryLookupDialog::CoverColumn)->text(),
			QStringLiteral("Not available"));
		QVERIFY(errors.isEmpty());
		// Logged even so.
		QCOMPARE(m_console.size(), 1);
	}

	void cancelStopsFurtherRequests()
	{
		serveHobbit();
		const auto client = makeClient();
		client->setMinimumInterval(300);
		{
			OpenLibraryLookupDialog dialog(*client, OpenLibraryQuery{QStringLiteral("The Hobbit"), QString(), QString()});
			QTRY_COMPARE(dialog.resultsTable()->rowCount(), 2);
			dialog.reject();
		}
		QTest::qWait(700);
		// The search, then at most the one cover already sent.
		QVERIFY(m_server->requests().size() <= 2);
		QCOMPARE(client->pendingCount(), 0);
	}

	// ---- The book editor ----

	void lookupButtonOnlyWhenAdding()
	{
		const auto database = test::freshDatabase();
		const auto client = makeClient();
		BookEditorDialog adding(*database, Book(), nullptr, client.get());
		QVERIFY(adding.lookupButton());
		QVERIFY(!adding.lookupButton()->autoDefault());

		BookEditorDialog withoutClient(*database, Book());
		QVERIFY(!withoutClient.lookupButton());

		const QString id = test::addBook(*database, QStringLiteral("Existing"));
		const auto book = BookRepository(*database).find(id);
		QVERIFY(book);
		BookEditorDialog editing(*database, *book, nullptr, client.get());
		QVERIFY(!editing.lookupButton());
	}

	void mainWindowGivesAddBookTheClient()
	{
		const auto database = test::freshDatabase();
		const auto settings = freshSettings();
		MainWindow window(*database, *settings);
		QVERIFY(window.openLibraryClient());
		QCOMPARE(window.openLibraryClient()->minimumIntervalMs(), 1000);
		QCOMPARE(window.openLibraryClient()->apiBase(), OpenLibrary::defaultApiBase());
	}

	void lookupQueryComesFromTheForm()
	{
		const auto database = test::freshDatabase();
		const auto client = makeClient();
		BookEditorDialog editor(*database, Book(), nullptr, client.get());
		editor.titleEdit()->setText(QStringLiteral("  Dune "));
		editor.authorsEditor()->picker()->setEditText(QStringLiteral("Frank Herbert"));
		editor.isbnEdit()->setText(QStringLiteral("978-0-441-17271-9"));
		OpenLibraryQuery query = editor.lookupQuery();
		QCOMPARE(query.title, QStringLiteral("Dune"));
		QCOMPARE(query.author, QStringLiteral("Frank Herbert"));
		QCOMPARE(query.isbn, QStringLiteral("978-0-441-17271-9"));

		// An author already in the list is used ahead of the picker text.
		editor.authorsEditor()->addCurrentText();
		editor.authorsEditor()->picker()->setEditText(QStringLiteral("Someone Else"));
		QCOMPARE(editor.lookupQuery().author, QStringLiteral("Frank Herbert"));
	}

	void emptyFormAsksForDetailsFirst()
	{
		const auto database = test::freshDatabase();
		const auto client = makeClient();
		BookEditorDialog editor(*database, Book(), nullptr, client.get());
		QString shown;
		QTimer::singleShot(0, [&shown] {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
				shown = box->text();
				box->accept();
			}
		});
		editor.lookUpOnOpenLibrary();
		QVERIFY(shown.contains(QStringLiteral("Type what you know")));
		// Nothing was sent.
		QTest::qWait(50);
		QVERIFY(m_server->requests().isEmpty());
	}

	void badIsbnAloneIsRefused()
	{
		const auto database = test::freshDatabase();
		const auto client = makeClient();
		BookEditorDialog editor(*database, Book(), nullptr, client.get());
		editor.isbnEdit()->setText(QStringLiteral("12345"));
		QString shown;
		QTimer::singleShot(0, [&shown] {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
				shown = box->text();
				box->accept();
			}
		});
		editor.lookUpOnOpenLibrary();
		QVERIFY(shown.contains(QStringLiteral("10 or 13 digits")));
		QTest::qWait(50);
		QVERIFY(m_server->requests().isEmpty());
	}

	void buttonRunsTheWholeLookup()
	{
		serveHobbit();
		const auto database = test::freshDatabase();
		const auto client = makeClient();
		BookEditorDialog editor(*database, Book(), nullptr, client.get());
		editor.titleEdit()->setText(QStringLiteral("hobbit"));

		// Once the lookup dialog is up and has results, pick the first.
		QTimer poll;
		connect(&poll, &QTimer::timeout, [&poll] {
			auto* dialog = qobject_cast<OpenLibraryLookupDialog*>(QApplication::activeModalWidget());
			if (dialog && dialog->resultsTable()->rowCount() > 0 && dialog->chooseButton()->isEnabled()) {
				poll.stop();
				dialog->chooseButton()->click();
			}
		});
		poll.start(20);
		editor.lookupButton()->click();

		QCOMPARE(editor.titleEdit()->text(), QStringLiteral("The Hobbit: or There and Back Again"));
		QCOMPARE(editor.isbnEdit()->text(), QStringLiteral("9780261103344"));
		QCOMPARE(editor.pagesEdit()->value(), 310);
	}

	void applyLookupFillsTheForm()
	{
		const auto database = test::freshDatabase();
		// An author and genres the user already has, to be linked rather
		// than duplicated.
		const QString tolkienId = AuthorRepository(*database).findOrCreate(QStringLiteral("J.R.R. Tolkien"));
		const QString fantasyId = LookupRepository(*database, LookupKind::Genre).findOrCreate(QStringLiteral("fantasy"));
		LookupRepository(*database, LookupKind::Genre).findOrCreate(QStringLiteral("Romance"));

		BookEditorDialog editor(*database, Book());
		editor.authorsEditor()->picker()->setEditText(QStringLiteral("Tolkein (typo)"));
		editor.authorsEditor()->addCurrentText();
		editor.privateCheck()->setChecked(true);

		OpenLibraryBook found;
		found.title = QStringLiteral("The Hobbit");
		found.authors = {QStringLiteral("j.r.r. tolkien"), QStringLiteral("Christopher Tolkien")};
		found.isbn = QStringLiteral("9780261103344");
		found.releaseDate = QStringLiteral("1997-11-03");
		found.pageCount = 310;
		found.physicalFormat = QStringLiteral("Hardback");
		found.description = QStringLiteral("A hobbit goes on an adventure.");
		found.subjects = {QStringLiteral("Fantasy"), QStringLiteral("Dragons")};
		found.coverImage = coverPng(QSize(1200, 1800));
		editor.applyLookup(found);

		QCOMPARE(editor.titleEdit()->text(), QStringLiteral("The Hobbit"));
		const QList<LinkChoice> authors = editor.authorsEditor()->selected();
		QCOMPARE(authors.size(), 2);
		// Linked to the existing author, with the existing spelling.
		QCOMPARE(authors.at(0).id, tolkienId);
		QCOMPARE(authors.at(0).name, QStringLiteral("J.R.R. Tolkien"));
		QVERIFY(authors.at(1).id.isEmpty());
		QCOMPARE(authors.at(1).name, QStringLiteral("Christopher Tolkien"));

		QCOMPARE(editor.isbnEdit()->text(), QStringLiteral("9780261103344"));
		QCOMPARE(editor.releaseDateEdit()->text(), QStringLiteral("1997-11-03"));
		QCOMPARE(editor.pagesEdit()->value(), 310);
		QCOMPARE(editor.bookTypeCombo()->currentText(), QStringLiteral("Hardcover"));
		QCOMPARE(editor.descriptionEdit()->toPlainText(), QStringLiteral("A hobbit goes on an adventure."));

		// Only the subject that is already a genre; no new "Dragons" genre.
		const QList<LinkChoice> genres = editor.genresEditor()->selected();
		QCOMPARE(genres.size(), 1);
		QCOMPARE(genres.first().id, fantasyId);

		// The cover is scaled for storage like a chosen file would be.
		QImage stored;
		QVERIFY(stored.loadFromData(editor.coverImage()));
		QVERIFY(stored.width() <= 600 && stored.height() <= 900);

		// What is about the user's copy is left alone.
		QVERIFY(editor.privateCheck()->isChecked());

		// And the result saves.
		editor.accept();
		QCOMPARE(int(editor.result()), int(QDialog::Accepted));
		const auto saved = BookRepository(*database).find(editor.savedBook().id);
		QVERIFY(saved);
		QCOMPARE(saved->authorIds.first(), tolkienId);
		QCOMPARE(saved->pageCount, std::optional<int>(310));
		QCOMPARE(test::rowCount(*database, QStringLiteral("genres")), 2);
	}

	void applyLookupKeepsWhatWasNotFound()
	{
		const auto database = test::freshDatabase();
		BookEditorDialog editor(*database, Book());
		editor.titleEdit()->setText(QStringLiteral("Typed title"));
		editor.isbnEdit()->setText(QStringLiteral("0306406152"));
		editor.authorsEditor()->picker()->setEditText(QStringLiteral("Typed Author"));
		editor.authorsEditor()->addCurrentText();
		editor.pagesEdit()->setValue(99);
		const int typeIndex = editor.bookTypeCombo()->findText(QStringLiteral("E-book"));
		editor.bookTypeCombo()->setCurrentIndex(typeIndex);

		OpenLibraryBook found;
		found.description = QStringLiteral("Only this.");
		// A format that is not one of the user's types changes nothing.
		found.physicalFormat = QStringLiteral("Scroll");
		editor.applyLookup(found);

		QCOMPARE(editor.titleEdit()->text(), QStringLiteral("Typed title"));
		QCOMPARE(editor.isbnEdit()->text(), QStringLiteral("0306406152"));
		QCOMPARE(editor.authorsEditor()->selected().size(), 1);
		QCOMPARE(editor.pagesEdit()->value(), 99);
		QCOMPARE(editor.bookTypeCombo()->currentIndex(), typeIndex);
		QCOMPARE(editor.descriptionEdit()->toPlainText(), QStringLiteral("Only this."));
		QVERIFY(editor.coverImage().isEmpty());
	}

	void unreadableCoverIsReportedAndLogged()
	{
		const auto database = test::freshDatabase();
		BookEditorDialog editor(*database, Book());
		OpenLibraryBook found;
		found.title = QStringLiteral("Broken cover");
		found.coverImage = QByteArrayLiteral("not an image");
		QString shown;
		QTimer::singleShot(0, [&shown] {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
				shown = box->text();
				box->accept();
			}
		});
		editor.applyLookup(found);
		QVERIFY(shown.contains(QStringLiteral("could not be read")));
		QCOMPARE(editor.titleEdit()->text(), QStringLiteral("Broken cover"));
		QVERIFY(editor.coverImage().isEmpty());
		QCOMPARE(m_console.size(), 1);
	}

	// ---- Settings ----

	void settingsDefaults()
	{
		const auto settings = freshSettings();
		SettingsDialog dialog(*settings);
		QVERIFY(dialog.consoleCheck()->isChecked());
		QVERIFY(!dialog.fileCheck()->isChecked());
	}

	void settingsNameTheLogFile()
	{
		const auto settings = freshSettings();
		const QString path = m_folder.filePath(QStringLiteral("named.log"));
		ErrorLog::instance().setFilePath(path);
		SettingsDialog dialog(*settings);
		QVERIFY(dialog.fileCheck()->text().contains(QStringLiteral("named.log")));
		const QString location = dialog.fileLocationLabel()->text();
		QVERIFY(location.contains(QStringLiteral("File name: named.log")));
		QVERIFY(location.contains(QDir::toNativeSeparators(m_folder.path())));
		QVERIFY(location.contains(QDir::toNativeSeparators(path)));
	}

	void settingsAreSavedAndApplied()
	{
		const auto settings = freshSettings();
		{
			SettingsDialog dialog(*settings);
			dialog.consoleCheck()->setChecked(false);
			dialog.fileCheck()->setChecked(true);
			dialog.accept();
		}
		QCOMPARE(settings->value(QLatin1String(SettingsDialog::LogToConsoleKey)).toBool(), false);
		QCOMPARE(settings->value(QLatin1String(SettingsDialog::LogToFileKey)).toBool(), true);
		QVERIFY(!ErrorLog::instance().consoleEnabled());
		QVERIFY(ErrorLog::instance().fileEnabled());

		// Shown again as saved.
		SettingsDialog again(*settings);
		QVERIFY(!again.consoleCheck()->isChecked());
		QVERIFY(again.fileCheck()->isChecked());
	}

	void settingsCancelChangesNothing()
	{
		const auto settings = freshSettings();
		SettingsDialog dialog(*settings);
		dialog.consoleCheck()->setChecked(false);
		dialog.reject();
		QVERIFY(!settings->contains(QLatin1String(SettingsDialog::LogToConsoleKey)));
		QVERIFY(ErrorLog::instance().consoleEnabled());
	}

	void savedSettingsAreAppliedAtStartup()
	{
		const auto settings = freshSettings();
		SettingsDialog::applySavedSettings(*settings);
		QVERIFY(ErrorLog::instance().consoleEnabled());
		QVERIFY(!ErrorLog::instance().fileEnabled());

		settings->setValue(QLatin1String(SettingsDialog::LogToConsoleKey), false);
		settings->setValue(QLatin1String(SettingsDialog::LogToFileKey), true);
		SettingsDialog::applySavedSettings(*settings);
		QVERIFY(!ErrorLog::instance().consoleEnabled());
		QVERIFY(ErrorLog::instance().fileEnabled());
	}

	void settingsAreInTheFileMenu()
	{
		const auto database = test::freshDatabase();
		const auto settings = freshSettings();
		MainWindow window(*database, *settings);
		QVERIFY(window.settingsAction());
		QCOMPARE(window.settingsAction()->text(), QStringLiteral("&Settings..."));
		QVERIFY(!window.settingsAction()->associatedObjects().isEmpty());
	}

	void databaseErrorsAreLogged()
	{
		QWidget parent;
		QTimer::singleShot(0, [] {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
				box->accept();
		});
		const bool ok = runReportingErrors(&parent, QStringLiteral("Delete Book"),
			[] { throw DatabaseError(QStringLiteral("It failed.\nDELETE FROM books")); });
		QVERIFY(!ok);
		QCOMPARE(m_console.size(), 1);
		QVERIFY(m_console.first().contains(QStringLiteral("While: Delete Book")));
		QVERIFY(m_console.first().contains(QStringLiteral("DELETE FROM books")));
	}
};

QTEST_MAIN(TestOpenLibraryGui)
#include "tst_open_library_gui.moc"
