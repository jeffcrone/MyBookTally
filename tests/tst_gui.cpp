// Tests for the windows and dialogs (src/app). They run with the
// "offscreen" platform, so nothing appears on screen and no display is
// needed. Dialogs are driven through their widgets the way a user would,
// then accepted, and the database is checked for the result.
//
// Not covered here: the file pickers and message boxes, which are modal
// system dialogs that a test cannot click through. The import and export
// logic behind them is tested directly in tst_json_exchange.

#include "BookDetailsDialog.h"
#include "BookEditorDialog.h"
#include "BookTableModel.h"
#include "ColumnFitter.h"
#include "LinkListEditor.h"
#include "ListRepository.h"
#include "MainWindow.h"
#include "ManageDialog.h"
#include "ReviewRepository.h"
#include "SmallDialogs.h"
#include "TestSupport.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>

using namespace mbt;

class TestGui : public QObject
{
	Q_OBJECT

private:
	// A settings file of its own for each test, so window layouts saved by
	// one test (or by a real run of the app) cannot affect another.
	std::unique_ptr<QSettings> freshSettings()
	{
		return std::make_unique<QSettings>(
			m_folder.filePath(QStringLiteral("settings-%1.ini").arg(++m_settingsCount)),
			QSettings::IniFormat);
	}

	QTemporaryDir m_folder;
	int m_settingsCount = 0;

private slots:
	void initTestCase()
	{
		QVERIFY(m_folder.isValid());
	}

	// ---- Main window ----

	void mainWindowShowsDefaultColumns()
	{
		const auto database = test::freshDatabase();
		const auto settings = freshSettings();
		MainWindow window(*database, *settings);
		QHeaderView* header = window.bookView()->horizontalHeader();
		for (int column = 0; column < int(BookColumn::Count); ++column) {
			const bool expected = BookTableModel::isVisibleByDefault(BookColumn(column));
			QCOMPARE(!header->isSectionHidden(column), expected);
			QCOMPARE(window.columnActions().at(column)->isChecked(), expected);
		}
	}

	void mainWindowColumnChoiceIsRemembered()
	{
		const auto database = test::freshDatabase();
		const auto settings = freshSettings();
		const int isbn = int(BookColumn::Isbn);
		{
			MainWindow window(*database, *settings);
			window.columnActions().at(isbn)->setChecked(true);
			QVERIFY(!window.bookView()->horizontalHeader()->isSectionHidden(isbn));
			window.columnActions().at(int(BookColumn::Authors))->setChecked(false);
			window.close();
		}
		MainWindow again(*database, *settings);
		QVERIFY(!again.bookView()->horizontalHeader()->isSectionHidden(isbn));
		QVERIFY(again.bookView()->horizontalHeader()->isSectionHidden(int(BookColumn::Authors)));
		QVERIFY(again.columnActions().at(isbn)->isChecked());
	}

	void mainWindowTitleColumnCannotBeHidden()
	{
		const auto database = test::freshDatabase();
		const auto settings = freshSettings();
		MainWindow window(*database, *settings);
		QVERIFY(!window.columnActions().at(int(BookColumn::Title))->isEnabled());
	}

	void defaultWindowSizeIsWideButFitsTheScreen()
	{
		// A large screen gets the full preferred size.
		QCOMPARE(MainWindow::defaultWindowSize(QRect(0, 0, 2560, 1400)), QSize(1280, 780));
		// A small one gets 90% of it, so the window never spills off screen.
		QCOMPARE(MainWindow::defaultWindowSize(QRect(0, 0, 1000, 700)), QSize(900, 630));
	}

	void mainWindowOpensAtDefaultSizeOnFirstRun()
	{
		const auto database = test::freshDatabase();
		const auto settings = freshSettings();
		MainWindow window(*database, *settings);
		const QRect available = window.screen()->availableGeometry();
		QCOMPARE(window.size(), MainWindow::defaultWindowSize(available));
	}

	void mainWindowIgnoresSizeSavedByVersion001()
	{
		// 0.0.1 saved its small first-run size under the old key; that must
		// not stop the window opening at the new default size.
		const auto database = test::freshDatabase();
		const auto settings = freshSettings();
		{
			MainWindow old(*database, *settings);
			old.resize(400, 300);
			settings->setValue("mainWindow/geometry", old.saveGeometry());
		}
		MainWindow window(*database, *settings);
		QCOMPARE(window.size(), MainWindow::defaultWindowSize(window.screen()->availableGeometry()));
		QVERIFY(!settings->contains("mainWindow/geometry"));
	}

	void mainWindowRemembersItsSize()
	{
		const auto database = test::freshDatabase();
		const auto settings = freshSettings();
		{
			MainWindow window(*database, *settings);
			window.resize(640, 480);
			window.close();
		}
		MainWindow again(*database, *settings);
		QCOMPARE(again.size(), QSize(640, 480));
	}

	void mainWindowColumnsFillTheTable()
	{
		const auto database = test::freshDatabase();
		test::addBook(*database, "A Book", {"An Author"});
		const auto settings = freshSettings();
		MainWindow window(*database, *settings);
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));

		QTableView* table = window.bookView();
		QHeaderView* header = table->horizontalHeader();
		auto visibleWidth = [&] {
			int total = 0;
			for (int column = 0; column < header->count(); ++column)
				if (!header->isSectionHidden(column))
					total += header->sectionSize(column);
			return total;
		};
		QTRY_COMPARE(visibleWidth(), table->viewport()->width());
		// Title gets more room than Authors by default.
		QVERIFY(header->sectionSize(int(BookColumn::Title)) > header->sectionSize(int(BookColumn::Authors)));

		// Turning on more columns shares the same width among them.
		window.columnActions().at(int(BookColumn::Isbn))->setChecked(true);
		window.columnActions().at(int(BookColumn::PageCount))->setChecked(true);
		QCOMPARE(visibleWidth(), table->viewport()->width());
		for (int column = 0; column < header->count(); ++column)
			if (!header->isSectionHidden(column))
				QVERIFY(header->sectionSize(column) >= window.columnFitter()->minimumWidth(column));
	}

	void mainWindowListsBooksAndHidesPrivate()
	{
		const auto database = test::freshDatabase();
		test::addBook(*database, "Visible", {"An Author"});
		test::addBook(*database, "Hidden", {}, [](Book& b) { b.isPrivate = true; });
		const auto settings = freshSettings();
		MainWindow window(*database, *settings);

		QCOMPARE(window.bookModel()->rowCount(), 1);
		QCOMPARE(window.bookModel()->books().first().title, QString("Visible"));
		QCOMPARE(window.bookModel()->books().first().authors, QString("An Author"));

		window.showPrivateAction()->setChecked(true);
		QCOMPARE(window.bookModel()->rowCount(), 2);
	}

	void mainWindowSearchFilters()
	{
		const auto database = test::freshDatabase();
		test::addBook(*database, "The Hobbit", {"J. R. R. Tolkien"});
		test::addBook(*database, "Dune", {"Frank Herbert"});
		const auto settings = freshSettings();
		MainWindow window(*database, *settings);

		window.searchEdit()->setText("tolkien");
		QCOMPARE(window.proxyModel()->rowCount(), 1);
		QCOMPARE(window.proxyModel()->index(0, 0).data().toString(), QString("The Hobbit"));
		window.searchEdit()->clear();
		QCOMPARE(window.proxyModel()->rowCount(), 2);
	}

	void mainWindowSwitchesBetweenLists()
	{
		const auto database = test::freshDatabase();
		const QString inList = test::addBook(*database, "In The List");
		test::addBook(*database, "Not In The List");
		const ListRepository lists(*database);
		BookList list;
		list.name = "Picks";
		const QString listId = lists.create(list);
		lists.addBook(listId, inList);

		const auto settings = freshSettings();
		MainWindow window(*database, *settings);
		// "Library" plus the one list.
		QCOMPARE(window.listPanel()->count(), 2);
		QVERIFY(window.listPanel()->item(1)->text().startsWith("Picks"));
		QCOMPARE(window.bookModel()->rowCount(), 2);

		window.selectList(listId);
		QCOMPARE(window.currentListId(), listId);
		QCOMPARE(window.bookModel()->rowCount(), 1);
		QCOMPARE(window.bookModel()->bookId(0), inList);

		window.selectList(QString());
		QCOMPARE(window.bookModel()->rowCount(), 2);
	}

	void mainWindowRefreshKeepsSelection()
	{
		const auto database = test::freshDatabase();
		test::addBook(*database, "A");
		const QString b = test::addBook(*database, "B");
		const auto settings = freshSettings();
		MainWindow window(*database, *settings);

		const QModelIndex index = window.proxyModel()->mapFromSource(
			window.bookModel()->index(window.bookModel()->rowOf(b), 0));
		window.bookView()->selectionModel()->select(index,
			QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		QCOMPARE(window.selectedBookIds(), QList<QString>({b}));

		test::addBook(*database, "C");
		window.refresh();
		QCOMPARE(window.bookModel()->rowCount(), 3);
		QCOMPARE(window.selectedBookIds(), QList<QString>({b}));
	}

	// ---- Book editor ----

	void editorAddsBookWithNewLinks()
	{
		const auto database = test::freshDatabase();
		BookEditorDialog editor(*database, Book());
		editor.titleEdit()->setText("The Left Hand of Darkness");
		editor.authorsEditor()->picker()->setEditText("Ursula K. Le Guin");
		QVERIFY(editor.authorsEditor()->addCurrentText());
		editor.genresEditor()->picker()->setEditText("Science Fiction");
		QVERIFY(editor.genresEditor()->addCurrentText());
		editor.addSeriesRow("Hainish Cycle", "4", QString());
		editor.isbnEdit()->setText("978-0-441-47812-5");
		editor.releaseDateEdit()->setText("1969");
		editor.pagesEdit()->setValue(304);
		editor.bookTypeCombo()->setCurrentIndex(editor.bookTypeCombo()->findText("Paperback"));
		editor.privateCheck()->setChecked(true);
		editor.setConfirmChecksumWarnings(true);
		editor.accept();
		QCOMPARE(editor.result(), int(QDialog::Accepted));

		const auto book = BookRepository(*database).find(editor.savedBook().id);
		QVERIFY(book);
		QCOMPARE(book->title, QString("The Left Hand of Darkness"));
		QCOMPARE(AuthorRepository(*database).find(book->authorIds.first())->name,
			QString("Ursula K. Le Guin"));
		QCOMPARE(LookupRepository(*database, LookupKind::Genre).find(book->genreIds.first())->name,
			QString("Science Fiction"));
		QCOMPARE(book->series.first().seriesName, QString("Hainish Cycle"));
		QCOMPARE(*book->series.first().position, 4.0);
		QCOMPARE(book->isbn, QString("9780441478125"));
		QCOMPARE(book->releaseDate, QString("1969"));
		QCOMPARE(*book->pageCount, 304);
		QCOMPARE(LookupRepository(*database, LookupKind::BookType).find(book->bookTypeId)->name,
			QString("Paperback"));
		QVERIFY(book->isPrivate);
	}

	void editorAddsTypedButNotAddedNames()
	{
		// Typing an author and pressing OK without pressing Add still counts.
		const auto database = test::freshDatabase();
		BookEditorDialog editor(*database, Book());
		editor.titleEdit()->setText("Quick Entry");
		editor.authorsEditor()->picker()->setEditText("Typed Author");
		editor.accept();
		QCOMPARE(editor.result(), int(QDialog::Accepted));
		const auto book = BookRepository(*database).find(editor.savedBook().id);
		QCOMPARE(book->authorIds.size(), 1);
	}

	void editorReusesExistingAuthorByName()
	{
		const auto database = test::freshDatabase();
		const QString existing = AuthorRepository(*database).findOrCreate("Octavia E. Butler");
		BookEditorDialog editor(*database, Book());
		editor.titleEdit()->setText("Kindred");
		editor.authorsEditor()->picker()->setEditText("octavia e. butler");
		QVERIFY(editor.authorsEditor()->addCurrentText());
		// The same author twice is refused.
		editor.authorsEditor()->picker()->setEditText("Octavia E. Butler");
		QVERIFY(!editor.authorsEditor()->addCurrentText());
		editor.accept();
		QCOMPARE(BookRepository(*database).find(editor.savedBook().id)->authorIds,
			QList<QString>({existing}));
		QCOMPARE(test::rowCount(*database, "authors"), 1);
	}

	void editorRequiresTitle()
	{
		const auto database = test::freshDatabase();
		BookEditorDialog editor(*database, Book());
		// validate() shows a message box; close it as soon as it appears so
		// the test does not wait forever.
		QTimer::singleShot(0, [] {
			if (QWidget* box = QApplication::activeModalWidget())
				box->close();
		});
		editor.accept();
		QVERIFY(editor.result() != int(QDialog::Accepted));
		QCOMPARE(test::rowCount(*database, "books"), 0);
	}

	void editorUpdatesExistingBook()
	{
		const auto database = test::freshDatabase();
		const QString id = test::addBook(*database, "Old Title", {"Author A"});
		BookEditorDialog editor(*database, *BookRepository(*database).find(id));
		QCOMPARE(editor.titleEdit()->text(), QString("Old Title"));
		QCOMPARE(editor.authorsEditor()->selected().size(), 1);
		QCOMPARE(editor.authorsEditor()->selected().first().name, QString("Author A"));

		editor.titleEdit()->setText("New Title");
		editor.accept();
		QCOMPARE(BookRepository(*database).find(id)->title, QString("New Title"));
		QCOMPARE(test::rowCount(*database, "books"), 1);
	}

	// ---- Link list editor ----

	void linkEditorOrdering()
	{
		LinkListEditor editor(true, QString());
		editor.setChoices({{"1", "One"}, {"2", "Two"}});
		editor.setSelected({{"1", "One"}, {"2", "Two"}});
		editor.list()->setCurrentRow(1);
		// Move "Two" up, using the Up button the editor created.
		const QList<QPushButton*> buttons = editor.findChildren<QPushButton*>();
		for (QPushButton* button : buttons) {
			if (button->text() == "Up")
				button->click();
		}
		QCOMPARE(editor.selected(), QList<LinkChoice>({{"2", "Two"}, {"1", "One"}}));
	}

	void linkEditorNewNameHasNoId()
	{
		LinkListEditor editor(false, QString());
		editor.setChoices({{"1", "Existing"}});
		editor.picker()->setEditText("Brand New");
		QVERIFY(editor.addCurrentText());
		QCOMPARE(editor.selected(), QList<LinkChoice>({{QString(), "Brand New"}}));
		// The list shows it is new, but selected() returns the bare name.
		QVERIFY(editor.list()->item(0)->text().contains("new"));
		QVERIFY(!editor.addCurrentText()); // empty text now
	}

	// ---- Details dialog ----

	void detailsShowLinkedNames()
	{
		const auto database = test::freshDatabase();
		const QString id = test::addBook(*database, "Detailed Book", {"Writer Person"}, [&](Book& b) {
			b.genreIds = {LookupRepository(*database, LookupKind::Genre).findOrCreate("Mystery")};
			b.location = "Hall closet";
		});
		Rating rating;
		rating.bookId = id;
		rating.value = 4;
		ReviewRepository(*database).addRating(rating);
		BookList list;
		list.name = "Book Club";
		ListRepository lists(*database);
		lists.addBook(lists.create(list), id);

		BookDetailsDialog dialog(*database, id);
		const QString html = dialog.detailsHtml();
		QVERIFY(html.contains("Detailed Book"));
		QVERIFY(html.contains("Writer Person"));
		QVERIFY(html.contains("Mystery"));
		QVERIFY(html.contains("Hall closet"));
		QVERIFY(html.contains("Book Club"));
		QVERIFY(html.contains("4.0/5"));
		QCOMPARE(dialog.windowTitle(), QString("Detailed Book"));
		QVERIFY(!dialog.changed());
	}

	void detailsEscapeHtml()
	{
		const auto database = test::freshDatabase();
		const QString id = test::addBook(*database, "<b>Not bold</b>");
		BookDetailsDialog dialog(*database, id);
		QVERIFY(dialog.detailsHtml().contains("&lt;b&gt;Not bold"));
	}

	// ---- Manage dialog ----

	void manageGenresAddRenameDelete()
	{
		const auto database = test::freshDatabase();
		ManageDialog dialog(*database, LookupKind::Genre);
		QCOMPARE(dialog.itemList()->count(), 0);

		dialog.startNew();
		dialog.nameEdit()->setText("Horror");
		dialog.descriptionEdit()->setPlainText("Scary books");
		QVERIFY(dialog.saveCurrent());
		QCOMPARE(dialog.itemList()->count(), 1);
		const auto genre = LookupRepository(*database, LookupKind::Genre).findByName("Horror");
		QVERIFY(genre);
		QCOMPARE(genre->description, QString("Scary books"));

		// The saved item is selected, so editing and saving renames it.
		dialog.nameEdit()->setText("Gothic Horror");
		QVERIFY(dialog.saveCurrent());
		QCOMPARE(dialog.itemList()->count(), 1);
		QVERIFY(LookupRepository(*database, LookupKind::Genre).findByName("Gothic Horror"));

		QVERIFY(dialog.deleteCurrent(false));
		QCOMPARE(dialog.itemList()->count(), 0);
		QVERIFY(dialog.changed());
	}

	void manageAuthorsListsExisting()
	{
		const auto database = test::freshDatabase();
		AuthorRepository(*database).insert({QString(), "Mary Shelley", "Wrote Frankenstein.", "1797-08-30"});
		ManageDialog dialog(*database);
		QCOMPARE(dialog.itemList()->count(), 1);
		QCOMPARE(dialog.nameEdit()->text(), QString("Mary Shelley"));
		QCOMPARE(dialog.descriptionEdit()->toPlainText(), QString("Wrote Frankenstein."));
	}

	void manageBookTypesHasNoDescription()
	{
		const auto database = test::freshDatabase();
		ManageDialog dialog(*database, LookupKind::BookType);
		QCOMPARE(dialog.itemList()->count(), 5); // the seeded types
		QVERIFY(dialog.descriptionEdit() == nullptr);
	}

	// ---- Small dialogs ----

	void ratingDialogKeepsScoreWithinScale()
	{
		Rating rating;
		rating.value = 4;
		rating.scale = 5;
		RatingDialog dialog(rating);
		auto spins = dialog.findChildren<QDoubleSpinBox*>();
		QCOMPARE(spins.size(), 2);
		spins[0]->setValue(99); // clamped to the scale
		QCOMPARE(dialog.rating().value, 5.0);
	}

	void exportOptionsDefaults()
	{
		ExportOptionsDialog dialog(3);
		QVERIFY(dialog.options().includeIds);
		QVERIFY(dialog.options().includeCovers);
	}
};

QTEST_MAIN(TestGui)
#include "tst_gui.moc"
