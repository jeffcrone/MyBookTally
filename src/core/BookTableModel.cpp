#include "BookTableModel.h"

namespace mbt {

namespace {

QVariant textSortKey(const QString& text)
{
	return text.isEmpty() ? QVariant() : QVariant(text.toLower());
}

QString ratingText(const BookSummary& book)
{
	if (!book.averageRating)
		return QString();
	return QStringLiteral("%1/5 (%2)")
		.arg(QString::number(*book.averageRating, 'f', 1))
		.arg(book.ratingCount);
}

} // namespace

BookTableModel::BookTableModel(QObject* parent)
	: QAbstractTableModel(parent)
{
}

void BookTableModel::setBooks(const QList<BookSummary>& books)
{
	beginResetModel();
	m_books = books;
	endResetModel();
}

QString BookTableModel::bookId(int row) const
{
	if (row < 0 || row >= m_books.size())
		return QString();
	return m_books.at(row).id;
}

int BookTableModel::rowOf(const QString& bookId) const
{
	for (int row = 0; row < m_books.size(); ++row) {
		if (m_books.at(row).id == bookId)
			return row;
	}
	return -1;
}

int BookTableModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_books.size());
}

int BookTableModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(BookColumn::Count);
}

QVariant BookTableModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() >= m_books.size())
		return QVariant();
	const BookSummary& book = m_books.at(index.row());
	const auto column = static_cast<BookColumn>(index.column());

	if (role == BookIdRole)
		return book.id;

	if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
		switch (column) {
		case BookColumn::Title: return book.title;
		case BookColumn::Authors: return book.authors;
		case BookColumn::Series: return book.series;
		case BookColumn::Genres: return book.genres;
		case BookColumn::ReleaseDate: return book.releaseDate;
		case BookColumn::Isbn: return book.isbn;
		case BookColumn::PageCount:
			return book.pageCount ? QString::number(*book.pageCount) : QString();
		case BookColumn::BookType: return book.bookType;
		case BookColumn::OwnershipStatus: return book.ownershipStatus;
		case BookColumn::ReadingStatus: return book.readingStatus;
		case BookColumn::Location: return book.location;
		case BookColumn::Rating: return ratingText(book);
		case BookColumn::Private:
			return book.isPrivate ? tr("Private") : QString();
		case BookColumn::Description:
			// A table cell is one line; the tooltip shows the whole text.
			return role == Qt::DisplayRole
				? book.description.section(QLatin1Char('\n'), 0, 0)
				: book.description;
		case BookColumn::Count: break;
		}
		return QVariant();
	}

	if (role == SortRole) {
		switch (column) {
		case BookColumn::PageCount:
			return book.pageCount ? QVariant(*book.pageCount) : QVariant();
		case BookColumn::Rating:
			return book.averageRating ? QVariant(*book.averageRating) : QVariant();
		case BookColumn::Private:
			return book.isPrivate ? 1 : 0;
		default:
			// Every other column sorts by its displayed text.
			return textSortKey(data(index, Qt::DisplayRole).toString());
		}
	}

	if (role == Qt::TextAlignmentRole) {
		if (column == BookColumn::PageCount)
			return QVariant(Qt::AlignRight | Qt::AlignVCenter);
	}

	return QVariant();
}

QVariant BookTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return QVariant();
	if (section < 0 || section >= static_cast<int>(BookColumn::Count))
		return QVariant();
	return columnTitle(static_cast<BookColumn>(section));
}

QString BookTableModel::columnTitle(BookColumn column)
{
	switch (column) {
	case BookColumn::Title: return tr("Title");
	case BookColumn::Authors: return tr("Authors");
	case BookColumn::Series: return tr("Series");
	case BookColumn::Genres: return tr("Genres");
	case BookColumn::ReleaseDate: return tr("Released");
	case BookColumn::Isbn: return tr("ISBN");
	case BookColumn::PageCount: return tr("Pages");
	case BookColumn::BookType: return tr("Type");
	case BookColumn::OwnershipStatus: return tr("Ownership");
	case BookColumn::ReadingStatus: return tr("Reading Status");
	case BookColumn::Location: return tr("Location");
	case BookColumn::Rating: return tr("Rating");
	case BookColumn::Private: return tr("Private");
	case BookColumn::Description: return tr("Description");
	case BookColumn::Count: break;
	}
	return QString();
}

bool BookTableModel::isVisibleByDefault(BookColumn column)
{
	return column == BookColumn::Title || column == BookColumn::Authors;
}

} // namespace mbt
