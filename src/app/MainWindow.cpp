#include "MainWindow.h"

#include "BookDetailsDialog.h"
#include "BookEditorDialog.h"
#include "BookRepository.h"
#include "BookTableModel.h"
#include "Database.h"
#include "DatabaseError.h"
#include "JsonExchange.h"
#include "ListRepository.h"
#include "LookupRepository.h"
#include "ManageDialog.h"
#include "SmallDialogs.h"
#include "SqlUtil.h"
#include "UiUtil.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>

namespace mbt {

namespace {

constexpr int ListIdRole = Qt::UserRole;

// Settings keys. Bump the header key's suffix if the set of columns ever
// changes, so an old saved layout is not applied to the wrong columns.
const char* const geometryKey = "mainWindow/geometry";
const char* const stateKey = "mainWindow/state";
const char* const splitterKey = "mainWindow/splitter";
const char* const headerKey = "bookTable/header.v1";
const char* const showPrivateKey = "bookTable/showPrivate";
const char* const lastFolderKey = "files/lastFolder";

} // namespace

MainWindow::MainWindow(Database& database, QSettings& settings, QWidget* parent)
	: QMainWindow(parent)
	, m_database(database)
	, m_settings(settings)
{
	setWindowTitle(QStringLiteral("MyBookTally"));
	setWindowIcon(appIcon());

	buildActions();
	buildLayout();
	buildMenus();
	restoreSettings();

	refresh();
}

void MainWindow::buildActions()
{
	m_addBook = new QAction(tr("&Add Book..."), this);
	m_addBook->setShortcut(QKeySequence::New);
	connect(m_addBook, &QAction::triggered, this, &MainWindow::addBook);

	m_editBook = new QAction(tr("&Edit Book..."), this);
	m_editBook->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
	connect(m_editBook, &QAction::triggered, this, &MainWindow::editBook);

	m_viewBook = new QAction(tr("&View Details..."), this);
	m_viewBook->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
	connect(m_viewBook, &QAction::triggered, this, &MainWindow::viewBook);

	m_deleteBook = new QAction(tr("&Delete Book"), this);
	m_deleteBook->setShortcut(QKeySequence::Delete);
	connect(m_deleteBook, &QAction::triggered, this, &MainWindow::deleteBooks);

	m_removeFromList = new QAction(tr("&Remove from This List"), this);
	connect(m_removeFromList, &QAction::triggered, this, &MainWindow::removeSelectedFromList);

	m_newList = new QAction(tr("&New List..."), this);
	m_newList->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
	connect(m_newList, &QAction::triggered, this, &MainWindow::newList);

	m_editList = new QAction(tr("&Rename or Describe List..."), this);
	connect(m_editList, &QAction::triggered, this, &MainWindow::editList);

	m_deleteList = new QAction(tr("&Delete List"), this);
	connect(m_deleteList, &QAction::triggered, this, &MainWindow::deleteList);

	m_import = new QAction(tr("&Import Books..."), this);
	connect(m_import, &QAction::triggered, this, &MainWindow::importBooks);

	m_exportAll = new QAction(tr("&Export Shown Books..."), this);
	connect(m_exportAll, &QAction::triggered, this, [this] { exportBooks(false); });

	m_exportSelected = new QAction(tr("Export &Selected Books..."), this);
	connect(m_exportSelected, &QAction::triggered, this, [this] { exportBooks(true); });

	m_showPrivate = new QAction(tr("Show &Private Books"), this);
	m_showPrivate->setCheckable(true);
	connect(m_showPrivate, &QAction::toggled, this, [this] { reloadBooks(); });

	for (int column = 0; column < int(BookColumn::Count); ++column) {
		auto* action = new QAction(BookTableModel::columnTitle(BookColumn(column)), this);
		action->setCheckable(true);
		// The title is how a row is recognized; hiding it would leave rows
		// with nothing to tell them apart, so it is always shown.
		if (BookColumn(column) == BookColumn::Title)
			action->setEnabled(false);
		connect(action, &QAction::toggled, this, [this, column](bool on) { setColumnVisible(column, on); });
		m_columnActions.append(action);
	}
}

void MainWindow::buildLayout()
{
	m_lists = new QListWidget(this);
	m_lists->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_lists, &QListWidget::currentRowChanged, this, [this] {
		reloadBooks();
		updateActions();
	});
	connect(m_lists, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
		QMenu menu(this);
		menu.addAction(m_newList);
		menu.addAction(m_editList);
		menu.addAction(m_deleteList);
		menu.exec(m_lists->viewport()->mapToGlobal(pos));
	});

	m_model = new BookTableModel(this);
	m_proxy = new QSortFilterProxyModel(this);
	m_proxy->setSourceModel(m_model);
	m_proxy->setSortRole(BookTableModel::SortRole);
	m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
	m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
	// Search every column, including hidden ones, so searching for an
	// author finds their books even when the Authors column is hidden.
	m_proxy->setFilterKeyColumn(-1);

	m_table = new QTableView(this);
	m_table->setModel(m_proxy);
	m_table->setSortingEnabled(true);
	m_table->sortByColumn(int(BookColumn::Title), Qt::AscendingOrder);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setAlternatingRowColors(true);
	m_table->setWordWrap(false);
	m_table->verticalHeader()->hide();
	m_table->horizontalHeader()->setSectionsMovable(true);
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->setContextMenuPolicy(Qt::CustomContextMenu);
	m_table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);

	// Double-click or Enter opens the book.
	connect(m_table, &QTableView::activated, this, &MainWindow::viewBook);
	connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
		&MainWindow::updateActions);
	connect(m_table, &QTableView::customContextMenuRequested, this, [this](const QPoint& pos) {
		QMenu menu(this);
		menu.addAction(m_viewBook);
		menu.addAction(m_editBook);
		menu.addSeparator();
		menu.addMenu(m_addToListMenu);
		if (!currentListId().isEmpty())
			menu.addAction(m_removeFromList);
		menu.addSeparator();
		menu.addAction(m_exportSelected);
		menu.addSeparator();
		menu.addAction(m_deleteBook);
		menu.exec(m_table->viewport()->mapToGlobal(pos));
	});
	// Right-clicking a column header offers the same column choices as the
	// View menu, which is where people tend to look for them.
	connect(m_table->horizontalHeader(), &QHeaderView::customContextMenuRequested, this,
		[this](const QPoint& pos) {
			QMenu menu(this);
			menu.addActions(m_columnActions);
			menu.exec(m_table->horizontalHeader()->mapToGlobal(pos));
		});

	m_search = new QLineEdit(this);
	m_search->setPlaceholderText(tr("Search titles, authors, series, genres..."));
	m_search->setClearButtonEnabled(true);
	connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
		m_proxy->setFilterFixedString(text);
		updateStatus();
	});

	auto* right = new QWidget(this);
	auto* rightLayout = new QVBoxLayout(right);
	rightLayout->setContentsMargins(0, 0, 0, 0);
	rightLayout->addWidget(m_search);
	rightLayout->addWidget(m_table);

	auto* splitter = new QSplitter(this);
	splitter->setObjectName(QStringLiteral("mainSplitter"));
	splitter->addWidget(m_lists);
	splitter->addWidget(right);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({200, 800});
	setCentralWidget(splitter);

	m_status = new QLabel(this);
	statusBar()->addWidget(m_status);

	m_addToListMenu = new QMenu(tr("Add to &List"), this);
	connect(m_addToListMenu, &QMenu::aboutToShow, this, &MainWindow::fillAddToListMenu);
}

void MainWindow::buildMenus()
{
	QMenu* file = menuBar()->addMenu(tr("&File"));
	file->addAction(m_import);
	file->addAction(m_exportAll);
	file->addAction(m_exportSelected);
	file->addSeparator();
	QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close);
	quit->setShortcut(QKeySequence::Quit);

	QMenu* book = menuBar()->addMenu(tr("&Book"));
	book->addAction(m_addBook);
	book->addAction(m_viewBook);
	book->addAction(m_editBook);
	book->addSeparator();
	book->addMenu(m_addToListMenu);
	book->addAction(m_removeFromList);
	book->addSeparator();
	book->addAction(m_deleteBook);

	QMenu* lists = menuBar()->addMenu(tr("&Lists"));
	lists->addAction(m_newList);
	lists->addAction(m_editList);
	lists->addAction(m_deleteList);

	QMenu* manage = menuBar()->addMenu(tr("&Manage"));
	manage->addAction(tr("&Authors..."), this, &MainWindow::manageAuthors);
	for (const LookupKind kind : allLookupKinds()) {
		manage->addAction(lookupKindPlural(kind) + QStringLiteral("..."), this,
			[this, kind] { manageLookup(int(kind)); });
	}

	QMenu* view = menuBar()->addMenu(tr("&View"));
	QMenu* columns = view->addMenu(tr("&Columns"));
	columns->addActions(m_columnActions);
	view->addAction(m_showPrivate);

	QMenu* help = menuBar()->addMenu(tr("&Help"));
	help->addAction(tr("&About MyBookTally"), this, &MainWindow::showAbout);
	help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);

	QToolBar* toolbar = addToolBar(tr("Main"));
	toolbar->setObjectName(QStringLiteral("mainToolBar"));
	toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
	toolbar->addAction(m_addBook);
	toolbar->addAction(m_viewBook);
	toolbar->addAction(m_editBook);
	toolbar->addAction(m_deleteBook);
	toolbar->addSeparator();
	toolbar->addAction(m_newList);
}

void MainWindow::restoreSettings()
{
	restoreGeometry(m_settings.value(QLatin1String(geometryKey)).toByteArray());
	restoreState(m_settings.value(QLatin1String(stateKey)).toByteArray());
	if (auto* splitter = findChild<QSplitter*>(QStringLiteral("mainSplitter")))
		splitter->restoreState(m_settings.value(QLatin1String(splitterKey)).toByteArray());

	QHeaderView* header = m_table->horizontalHeader();
	const QByteArray headerState = m_settings.value(QLatin1String(headerKey)).toByteArray();
	if (headerState.isEmpty() || !header->restoreState(headerState)) {
		// First run (or unreadable saved state): only the default columns.
		for (int column = 0; column < int(BookColumn::Count); ++column)
			header->setSectionHidden(column, !BookTableModel::isVisibleByDefault(BookColumn(column)));
		header->resizeSection(int(BookColumn::Title), 320);
	}
	// The title column must always be visible (see buildActions()).
	header->setSectionHidden(int(BookColumn::Title), false);

	// Match the View > Columns check marks to what is now shown. Signals are
	// blocked so this does not toggle the columns straight back.
	for (int column = 0; column < m_columnActions.size(); ++column) {
		QAction* action = m_columnActions.at(column);
		const QSignalBlocker blocker(action);
		action->setChecked(!header->isSectionHidden(column));
	}

	const QSignalBlocker blocker(m_showPrivate);
	m_showPrivate->setChecked(m_settings.value(QLatin1String(showPrivateKey), false).toBool());
}

void MainWindow::saveSettings()
{
	m_settings.setValue(QLatin1String(geometryKey), saveGeometry());
	m_settings.setValue(QLatin1String(stateKey), saveState());
	if (auto* splitter = findChild<QSplitter*>(QStringLiteral("mainSplitter")))
		m_settings.setValue(QLatin1String(splitterKey), splitter->saveState());
	m_settings.setValue(QLatin1String(headerKey), m_table->horizontalHeader()->saveState());
	m_settings.setValue(QLatin1String(showPrivateKey), m_showPrivate->isChecked());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
	saveSettings();
	event->accept();
}

void MainWindow::setColumnVisible(int column, bool visible)
{
	m_table->horizontalHeader()->setSectionHidden(column, !visible);
	// Save straight away, not only on close, so the choice survives a crash.
	m_settings.setValue(QLatin1String(headerKey), m_table->horizontalHeader()->saveState());
}

void MainWindow::refresh()
{
	reloadLists();
	reloadBooks();
}

void MainWindow::reloadLists()
{
	const QString selected = currentListId();
	m_lists->blockSignals(true);
	m_lists->clear();

	auto* library = new QListWidgetItem(tr("Library"), m_lists);
	library->setData(ListIdRole, QString());
	QFont bold = library->font();
	bold.setBold(true);
	library->setFont(bold);

	runReportingErrors(this, tr("Lists"), [&] {
		for (const BookList& list : ListRepository(m_database).all()) {
			auto* item = new QListWidgetItem(
				QStringLiteral("%1 (%2)").arg(list.name).arg(list.bookCount), m_lists);
			item->setData(ListIdRole, list.id);
			if (!list.description.isEmpty())
				item->setToolTip(list.description);
		}
	});

	int row = 0;
	for (int i = 0; i < m_lists->count(); ++i) {
		if (m_lists->item(i)->data(ListIdRole).toString() == selected)
			row = i;
	}
	m_lists->setCurrentRow(row);
	m_lists->blockSignals(false);
	updateActions();
}

void MainWindow::reloadBooks()
{
	const QList<QString> selected = selectedBookIds();
	runReportingErrors(this, tr("Books"), [&] {
		m_model->setBooks(BookRepository(m_database).summaries(m_showPrivate->isChecked(), currentListId()));
	});

	// Put the selection back on the same books, where they still exist.
	QItemSelection selection;
	for (const QString& id : selected) {
		const int sourceRow = m_model->rowOf(id);
		if (sourceRow < 0)
			continue;
		const QModelIndex index = m_proxy->mapFromSource(m_model->index(sourceRow, 0));
		if (index.isValid())
			selection.select(index, index);
	}
	m_table->selectionModel()->select(selection,
		QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

	updateStatus();
	updateActions();
}

void MainWindow::updateStatus()
{
	const int shown = m_proxy->rowCount();
	const int total = m_model->rowCount();
	if (shown == total)
		m_status->setText(tr("%n book(s)", nullptr, total));
	else
		m_status->setText(tr("%1 of %n book(s) match the search", nullptr, total).arg(shown));
}

void MainWindow::updateActions()
{
	const int count = int(selectedBookIds().size());
	const bool inList = !currentListId().isEmpty();
	m_editBook->setEnabled(count == 1);
	m_viewBook->setEnabled(count == 1);
	m_deleteBook->setEnabled(count > 0);
	m_addToListMenu->setEnabled(count > 0);
	m_removeFromList->setEnabled(count > 0 && inList);
	m_exportSelected->setEnabled(count > 0);
	m_editList->setEnabled(inList);
	m_deleteList->setEnabled(inList);
}

QString MainWindow::currentListId() const
{
	const QListWidgetItem* item = m_lists->currentItem();
	return item ? item->data(ListIdRole).toString() : QString();
}

void MainWindow::selectList(const QString& listId)
{
	for (int i = 0; i < m_lists->count(); ++i) {
		if (m_lists->item(i)->data(ListIdRole).toString() == listId) {
			m_lists->setCurrentRow(i);
			return;
		}
	}
}

QList<QString> MainWindow::selectedBookIds() const
{
	QList<QString> ids;
	if (!m_table || !m_table->selectionModel())
		return ids;
	QModelIndexList rows = m_table->selectionModel()->selectedRows();
	std::sort(rows.begin(), rows.end(),
		[](const QModelIndex& a, const QModelIndex& b) { return a.row() < b.row(); });
	for (const QModelIndex& index : rows)
		ids.append(index.data(BookTableModel::BookIdRole).toString());
	return ids;
}

// ---- Books ----------------------------------------------------------------

void MainWindow::addBook()
{
	BookEditorDialog editor(m_database, Book(), this);
	if (editor.exec() != QDialog::Accepted)
		return;
	const QString newId = editor.savedBook().id;
	// A book added while a list is showing goes into that list too, which is
	// what the user is looking at and almost certainly wants.
	if (!currentListId().isEmpty())
		runReportingErrors(this, tr("Add Book"),
			[&] { ListRepository(m_database).addBook(currentListId(), newId); });
	refresh();

	// Select the new book so the user sees where it went. It may be hidden
	// if it is private and private books are not shown.
	const int sourceRow = m_model->rowOf(newId);
	if (sourceRow >= 0) {
		const QModelIndex index = m_proxy->mapFromSource(m_model->index(sourceRow, 0));
		m_table->selectionModel()->select(index,
			QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		m_table->scrollTo(index);
	}
}

void MainWindow::editBook()
{
	const QList<QString> ids = selectedBookIds();
	if (ids.size() != 1)
		return;
	std::optional<Book> book;
	if (!runReportingErrors(this, tr("Edit Book"), [&] { book = BookRepository(m_database).find(ids.first()); })
		|| !book)
		return;
	BookEditorDialog editor(m_database, *book, this);
	if (editor.exec() == QDialog::Accepted)
		refresh();
}

void MainWindow::viewBook()
{
	const QList<QString> ids = selectedBookIds();
	if (ids.size() != 1)
		return;
	BookDetailsDialog details(m_database, ids.first(), this);
	details.exec();
	if (details.changed())
		refresh();
}

void MainWindow::deleteBooks()
{
	const QList<QString> ids = selectedBookIds();
	if (ids.isEmpty())
		return;

	QString question;
	if (ids.size() == 1) {
		const int row = m_model->rowOf(ids.first());
		question = tr("Delete \"%1\" from your library?").arg(m_model->books().at(row).title);
	} else {
		question = tr("Delete these %n books from your library?", nullptr, int(ids.size()));
	}
	question += QLatin1Char('\n') + tr("Their reviews and ratings are deleted too, "
		"and they are removed from every list. This cannot be undone.");
	if (QMessageBox::question(this, tr("Delete Book"), question) != QMessageBox::Yes)
		return;

	runReportingErrors(this, tr("Delete Book"), [&] {
		Transaction transaction(m_database.connection());
		const BookRepository books(m_database);
		for (const QString& id : ids)
			books.remove(id);
		transaction.commit();
	});
	refresh();
}

// ---- Lists ----------------------------------------------------------------

void MainWindow::fillAddToListMenu()
{
	m_addToListMenu->clear();
	runReportingErrors(this, tr("Add to List"), [&] {
		for (const BookList& list : ListRepository(m_database).all()) {
			// The list being viewed already holds the books.
			if (list.id == currentListId())
				continue;
			const QString id = list.id;
			m_addToListMenu->addAction(list.name, this, [this, id] { addSelectedToList(id); });
		}
	});
	if (!m_addToListMenu->isEmpty())
		m_addToListMenu->addSeparator();
	m_addToListMenu->addAction(tr("&New List..."), this, &MainWindow::addSelectedToNewList);
}

void MainWindow::addSelectedToList(const QString& listId)
{
	const QList<QString> ids = selectedBookIds();
	runReportingErrors(this, tr("Add to List"), [&] {
		Transaction transaction(m_database.connection());
		const ListRepository lists(m_database);
		for (const QString& id : ids)
			lists.addBook(listId, id);
		transaction.commit();
	});
	reloadLists();
}

void MainWindow::addSelectedToNewList()
{
	const QList<QString> ids = selectedBookIds();
	ListDialog dialog(BookList(), this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	QString listId;
	if (!runReportingErrors(this, dialog.windowTitle(),
			[&] { listId = ListRepository(m_database).create(dialog.list()); }))
		return;
	addSelectedToList(listId);
}

void MainWindow::removeSelectedFromList()
{
	const QString listId = currentListId();
	const QList<QString> ids = selectedBookIds();
	if (listId.isEmpty() || ids.isEmpty())
		return;
	runReportingErrors(this, tr("Remove from List"), [&] {
		Transaction transaction(m_database.connection());
		const ListRepository lists(m_database);
		for (const QString& id : ids)
			lists.removeBook(listId, id);
		transaction.commit();
	});
	refresh();
}

void MainWindow::newList()
{
	ListDialog dialog(BookList(), this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	QString listId;
	if (runReportingErrors(this, dialog.windowTitle(),
			[&] { listId = ListRepository(m_database).create(dialog.list()); })) {
		reloadLists();
		selectList(listId);
	}
}

void MainWindow::editList()
{
	const QString listId = currentListId();
	if (listId.isEmpty())
		return;
	std::optional<BookList> list;
	runReportingErrors(this, tr("Edit List"), [&] { list = ListRepository(m_database).find(listId); });
	if (!list)
		return;
	ListDialog dialog(*list, this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	if (runReportingErrors(this, dialog.windowTitle(),
			[&] { ListRepository(m_database).update(dialog.list()); }))
		reloadLists();
}

void MainWindow::deleteList()
{
	const QString listId = currentListId();
	if (listId.isEmpty())
		return;
	const QString name = m_lists->currentItem()->text();
	if (QMessageBox::question(this, tr("Delete List"),
			tr("Delete the list %1?\nThe books in it stay in your library.").arg(name))
		!= QMessageBox::Yes)
		return;
	if (runReportingErrors(this, tr("Delete List"), [&] { ListRepository(m_database).remove(listId); })) {
		selectList(QString());
		refresh();
	}
}

// ---- Manage ---------------------------------------------------------------

void MainWindow::manageAuthors()
{
	ManageDialog dialog(m_database, this);
	dialog.exec();
	if (dialog.changed())
		refresh();
}

void MainWindow::manageLookup(int kind)
{
	ManageDialog dialog(m_database, LookupKind(kind), this);
	dialog.exec();
	if (dialog.changed())
		refresh();
}

// ---- Import and export ----------------------------------------------------

void MainWindow::importBooks()
{
	const QString folder = m_settings.value(QLatin1String(lastFolderKey),
		QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
	const QString path = QFileDialog::getOpenFileName(this, tr("Import Books"), folder,
		tr("MyBookTally files (*.json);;All files (*)"));
	if (path.isEmpty())
		return;
	m_settings.setValue(QLatin1String(lastFolderKey), QFileInfo(path).absolutePath());

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		QMessageBox::warning(this, tr("Import Books"),
			tr("Could not open %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
		return;
	}
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		QMessageBox::warning(this, tr("Import Books"),
			tr("%1 is not a valid JSON file: %2").arg(QDir::toNativeSeparators(path), parseError.errorString()));
		return;
	}

	ImportResult result;
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const bool ok = runReportingErrors(this, tr("Import Books"),
		[&] { result = JsonExchange(m_database).importBooks(document); });
	QApplication::restoreOverrideCursor();
	if (!ok)
		return;
	refresh();

	QString message = tr("%n book(s) added", nullptr, result.booksAdded) + QStringLiteral(", ")
		+ tr("%n book(s) updated.", nullptr, result.booksUpdated);
	QMessageBox box(result.warnings.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
		tr("Import Books"), message, QMessageBox::Ok, this);
	if (!result.warnings.isEmpty()) {
		box.setInformativeText(tr("%n book(s) could not be imported. See the details.", nullptr,
			int(result.warnings.size())));
		box.setDetailedText(result.warnings.join(QLatin1Char('\n')));
	}
	box.exec();
}

void MainWindow::exportBooks(bool selectedOnly)
{
	// "Shown" books are the rows the table currently shows - the selected
	// list, narrowed by any search - in the order they are sorted.
	QList<QString> ids;
	if (selectedOnly) {
		ids = selectedBookIds();
	} else {
		for (int row = 0; row < m_proxy->rowCount(); ++row)
			ids.append(m_proxy->index(row, 0).data(BookTableModel::BookIdRole).toString());
	}
	if (ids.isEmpty()) {
		QMessageBox::information(this, tr("Export Books"), tr("There are no books to export."));
		return;
	}

	ExportOptionsDialog options(int(ids.size()), this);
	if (options.exec() != QDialog::Accepted)
		return;

	const QString folder = m_settings.value(QLatin1String(lastFolderKey),
		QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
	const QString suggested = QDir(folder).filePath(QStringLiteral("MyBookTally-export.json"));
	QString path = QFileDialog::getSaveFileName(this, tr("Export Books"), suggested,
		tr("MyBookTally files (*.json)"));
	if (path.isEmpty())
		return;
	if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
		path += QStringLiteral(".json");
	m_settings.setValue(QLatin1String(lastFolderKey), QFileInfo(path).absolutePath());

	QJsonDocument document;
	if (!runReportingErrors(this, tr("Export Books"),
			[&] { document = JsonExchange(m_database).exportBooks(ids, options.options()); }))
		return;

	// QSaveFile writes to a temporary file and renames it into place only
	// when everything was written, so a full disk never leaves a truncated
	// export where a good one used to be.
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(document.toJson(QJsonDocument::Indented)) < 0
		|| !file.commit()) {
		QMessageBox::warning(this, tr("Export Books"),
			tr("Could not write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
		return;
	}
	statusBar()->showMessage(tr("Exported %n book(s) to %1", nullptr, int(ids.size()))
		.arg(QDir::toNativeSeparators(path)), 8000);
}

void MainWindow::showAbout()
{
	QMessageBox::about(this, tr("About MyBookTally"),
		tr("<h3>MyBookTally %1</h3>"
		   "<p>Keep track of the books in your personal collection.</p>"
		   "<p>Your library is stored in:<br><tt>%2</tt></p>"
		   "<p>Built with Qt %3.</p>")
			.arg(QCoreApplication::applicationVersion(),
				QDir::toNativeSeparators(m_database.filePath()).toHtmlEscaped(),
				QString::fromLatin1(qVersion())));
}

} // namespace mbt
