#pragma once

#include <QList>
#include <QMainWindow>
#include <QString>

class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QSettings;
class QSortFilterProxyModel;
class QTableView;

namespace mbt {

class BookTableModel;
class Database;

// The main window: the user's lists on the left ("Library" for every book,
// then each list they have made), and the books in the selected one on the
// right, in a sortable, searchable table whose columns the user chooses.
class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	// settings is where window layout and column choices are remembered.
	// The app passes the user's settings; the tests pass a throwaway file so
	// they never disturb a real setup.
	MainWindow(Database& database, QSettings& settings, QWidget* parent = nullptr);

	// Re-reads the lists and books from the database, keeping the selection.
	void refresh();

	// Selects "Library" (empty id) or a list.
	void selectList(const QString& listId);
	// The selected list's id, or empty for the whole library.
	QString currentListId() const;
	// Ids of the books selected in the table, in table order.
	QList<QString> selectedBookIds() const;

	// Exposed for the automated tests.
	BookTableModel* bookModel() const { return m_model; }
	QSortFilterProxyModel* proxyModel() const { return m_proxy; }
	QTableView* bookView() const { return m_table; }
	QListWidget* listPanel() const { return m_lists; }
	QLineEdit* searchEdit() const { return m_search; }
	QAction* showPrivateAction() const { return m_showPrivate; }
	// The View > Columns action for each column, in column order.
	const QList<QAction*>& columnActions() const { return m_columnActions; }

protected:
	void closeEvent(QCloseEvent* event) override;

private:
	void buildActions();
	void buildMenus();
	void buildLayout();
	void restoreSettings();
	void saveSettings();
	void reloadLists();
	void reloadBooks();
	void updateActions();
	void updateStatus();
	void setColumnVisible(int column, bool visible);
	void fillAddToListMenu();

	void addBook();
	void editBook();
	void viewBook();
	void deleteBooks();
	void addSelectedToList(const QString& listId);
	void addSelectedToNewList();
	void removeSelectedFromList();
	void newList();
	void editList();
	void deleteList();
	void manageAuthors();
	void manageLookup(int kind);
	void importBooks();
	void exportBooks(bool selectedOnly);
	void showAbout();

	Database& m_database;
	QSettings& m_settings;

	BookTableModel* m_model = nullptr;
	QSortFilterProxyModel* m_proxy = nullptr;
	QTableView* m_table = nullptr;
	QListWidget* m_lists = nullptr;
	QLineEdit* m_search = nullptr;
	QLabel* m_status = nullptr;
	QMenu* m_addToListMenu = nullptr;

	QAction* m_addBook = nullptr;
	QAction* m_editBook = nullptr;
	QAction* m_viewBook = nullptr;
	QAction* m_deleteBook = nullptr;
	QAction* m_removeFromList = nullptr;
	QAction* m_newList = nullptr;
	QAction* m_editList = nullptr;
	QAction* m_deleteList = nullptr;
	QAction* m_import = nullptr;
	QAction* m_exportAll = nullptr;
	QAction* m_exportSelected = nullptr;
	QAction* m_showPrivate = nullptr;
	QList<QAction*> m_columnActions;
};

} // namespace mbt
