// Tests for the main table's model (src/core/BookTableModel.cpp).

#include "BookTableModel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTest>

using namespace mbt;

namespace {

BookSummary summary(const QString& id, const QString& title)
{
	BookSummary row;
	row.id = id;
	row.title = title;
	return row;
}

QModelIndex cell(const BookTableModel& model, int row, BookColumn column)
{
	return model.index(row, int(column));
}

} // namespace

class TestBookTableModel : public QObject
{
	Q_OBJECT

private slots:
	void passesModelTester()
	{
		// Qt's own checker for the QAbstractItemModel contract.
		BookTableModel model;
		QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
		model.setBooks({summary("1", "One"), summary("2", "Two")});
		model.setBooks({});
	}

	void shapeAndHeaders()
	{
		BookTableModel model;
		model.setBooks({summary("1", "One"), summary("2", "Two")});
		QCOMPARE(model.rowCount(), 2);
		QCOMPARE(model.columnCount(), int(BookColumn::Count));
		QCOMPARE(model.headerData(int(BookColumn::Title), Qt::Horizontal).toString(), QString("Title"));
		QCOMPARE(model.headerData(int(BookColumn::Authors), Qt::Horizontal).toString(), QString("Authors"));
		QVERIFY(!model.headerData(0, Qt::Vertical).isValid());
		for (int column = 0; column < int(BookColumn::Count); ++column)
			QVERIFY(!BookTableModel::columnTitle(BookColumn(column)).isEmpty());
	}

	void defaultColumns()
	{
		for (int column = 0; column < int(BookColumn::Count); ++column) {
			const auto c = BookColumn(column);
			QCOMPARE(BookTableModel::isVisibleByDefault(c),
				c == BookColumn::Title || c == BookColumn::Authors);
		}
	}

	void displaysEveryField()
	{
		BookSummary row = summary("id-1", "Title");
		row.authors = "A, B";
		row.series = "S #1";
		row.genres = "G";
		row.releaseDate = "2001";
		row.isbn = "9780306406157";
		row.pageCount = 321;
		row.bookType = "Paperback";
		row.ownershipStatus = "Have";
		row.readingStatus = "Read";
		row.location = "Shelf";
		row.description = "First line\nSecond line";
		row.averageRating = 4.26;
		row.ratingCount = 2;
		row.isPrivate = true;

		BookTableModel model;
		model.setBooks({row});
		auto text = [&](BookColumn c) { return cell(model, 0, c).data().toString(); };
		QCOMPARE(text(BookColumn::Title), QString("Title"));
		QCOMPARE(text(BookColumn::Authors), QString("A, B"));
		QCOMPARE(text(BookColumn::Series), QString("S #1"));
		QCOMPARE(text(BookColumn::Genres), QString("G"));
		QCOMPARE(text(BookColumn::ReleaseDate), QString("2001"));
		QCOMPARE(text(BookColumn::Isbn), QString("9780306406157"));
		QCOMPARE(text(BookColumn::PageCount), QString("321"));
		QCOMPARE(text(BookColumn::BookType), QString("Paperback"));
		QCOMPARE(text(BookColumn::OwnershipStatus), QString("Have"));
		QCOMPARE(text(BookColumn::ReadingStatus), QString("Read"));
		QCOMPARE(text(BookColumn::Location), QString("Shelf"));
		QCOMPARE(text(BookColumn::Rating), QString("4.3/5 (2)"));
		QCOMPARE(text(BookColumn::Private), QString("Private"));
		// One line in the cell, the whole text in the tooltip.
		QCOMPARE(text(BookColumn::Description), QString("First line"));
		QCOMPARE(cell(model, 0, BookColumn::Description).data(Qt::ToolTipRole).toString(),
			QString("First line\nSecond line"));
		QCOMPARE(cell(model, 0, BookColumn::Isbn).data(BookTableModel::BookIdRole).toString(),
			QString("id-1"));
	}

	void emptyValuesDisplayBlank()
	{
		BookTableModel model;
		model.setBooks({summary("1", "Bare")});
		QVERIFY(cell(model, 0, BookColumn::PageCount).data().toString().isEmpty());
		QVERIFY(cell(model, 0, BookColumn::Rating).data().toString().isEmpty());
		QVERIFY(cell(model, 0, BookColumn::Private).data().toString().isEmpty());
		QVERIFY(!cell(model, 0, BookColumn::PageCount).data(BookTableModel::SortRole).isValid());
	}

	void sortsNumbersNumerically()
	{
		BookSummary small = summary("s", "Small");
		small.pageCount = 20;
		BookSummary large = summary("l", "Large");
		large.pageCount = 100;
		BookSummary none = summary("n", "None");

		BookTableModel model;
		model.setBooks({large, none, small});
		QSortFilterProxyModel proxy;
		proxy.setSourceModel(&model);
		proxy.setSortRole(BookTableModel::SortRole);
		proxy.sort(int(BookColumn::PageCount), Qt::AscendingOrder);
		// As text "100" would sort before "20"; as numbers it does not.
		QCOMPARE(proxy.index(0, 0).data().toString(), QString("Small"));
		QCOMPARE(proxy.index(1, 0).data().toString(), QString("Large"));
		// Books with no page count go last.
		QCOMPARE(proxy.index(2, 0).data().toString(), QString("None"));
	}

	void sortsTextIgnoringCase()
	{
		BookTableModel model;
		model.setBooks({summary("1", "banana"), summary("2", "Apple"), summary("3", "cherry")});
		QSortFilterProxyModel proxy;
		proxy.setSourceModel(&model);
		proxy.setSortRole(BookTableModel::SortRole);
		proxy.sort(int(BookColumn::Title), Qt::AscendingOrder);
		QCOMPARE(proxy.index(0, 0).data().toString(), QString("Apple"));
		QCOMPARE(proxy.index(1, 0).data().toString(), QString("banana"));
		QCOMPARE(proxy.index(2, 0).data().toString(), QString("cherry"));
	}

	void sortsRatingsNumerically()
	{
		BookSummary low = summary("l", "Low");
		low.averageRating = 2.0;
		low.ratingCount = 1;
		BookSummary high = summary("h", "High");
		high.averageRating = 4.5;
		high.ratingCount = 1;
		BookTableModel model;
		model.setBooks({low, high});
		QSortFilterProxyModel proxy;
		proxy.setSourceModel(&model);
		proxy.setSortRole(BookTableModel::SortRole);
		proxy.sort(int(BookColumn::Rating), Qt::DescendingOrder);
		QCOMPARE(proxy.index(0, 0).data().toString(), QString("High"));
	}

	void rowLookup()
	{
		BookTableModel model;
		model.setBooks({summary("a", "A"), summary("b", "B")});
		QCOMPARE(model.rowOf("b"), 1);
		QCOMPARE(model.rowOf("zzz"), -1);
		QCOMPARE(model.bookId(0), QString("a"));
		QVERIFY(model.bookId(5).isEmpty());
		QVERIFY(model.bookId(-1).isEmpty());
	}

	void setBooksResetsModel()
	{
		BookTableModel model;
		QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
		model.setBooks({summary("a", "A")});
		QCOMPARE(reset.count(), 1);
	}
};

QTEST_GUILESS_MAIN(TestBookTableModel)
#include "tst_book_table_model.moc"
