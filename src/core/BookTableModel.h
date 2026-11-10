#pragma once

#include "Entities.h"

#include <QAbstractTableModel>
#include <QList>

namespace mbt {

// The columns the main book table can show: every book field except the
// internal id and the cover image, plus the names of linked records.
// Their order here is the order of the model's columns.
enum class BookColumn
{
	Title,
	Authors,
	Series,
	Genres,
	ReleaseDate,
	Isbn,
	PageCount,
	BookType,
	OwnershipStatus,
	ReadingStatus,
	Location,
	Rating,
	Private,
	Description,
	Count, // not a column; the number of columns
};

// Feeds BookSummary rows to the main window's table view.
//
// It lives in the core library rather than with the widgets because it has
// no widget code in it, and that lets the tests check what each column
// shows and how it sorts without opening a window.
class BookTableModel : public QAbstractTableModel
{
	Q_OBJECT

public:
	// The role the view sorts by. Numbers come back as numbers (so 100 pages
	// sorts after 20), text lowercased (so sorting ignores case), and an
	// empty value as nothing (so books missing a field group together).
	static constexpr int SortRole = Qt::UserRole + 1;
	// The book's id, for any column.
	static constexpr int BookIdRole = Qt::UserRole + 2;

	explicit BookTableModel(QObject* parent = nullptr);

	void setBooks(const QList<BookSummary>& books);
	const QList<BookSummary>& books() const { return m_books; }
	QString bookId(int row) const;
	// The row showing this book, or -1.
	int rowOf(const QString& bookId) const;

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

	static QString columnTitle(BookColumn column);
	// Title and author are shown on a first run; the rest are opt-in.
	static bool isVisibleByDefault(BookColumn column);

private:
	QList<BookSummary> m_books;
};

} // namespace mbt
