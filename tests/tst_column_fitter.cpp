// Tests for fitting the book table's columns to its width
// (src/app/ColumnFitter.cpp).

#include "ColumnFitter.h"

#include <QHeaderView>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTableView>
#include <QTest>

#include <numeric>

using namespace mbt;

namespace {

int sum(const QList<int>& values)
{
	return std::accumulate(values.begin(), values.end(), 0);
}

// Total width of the visible columns.
int visibleWidth(const QTableView& view)
{
	const QHeaderView* header = view.horizontalHeader();
	int total = 0;
	for (int column = 0; column < header->count(); ++column) {
		if (!header->isSectionHidden(column))
			total += header->sectionSize(column);
	}
	return total;
}

// A shown table with short column titles, so the header text never forces
// a minimum larger than the ones the tests set.
struct Fixture
{
	QStandardItemModel model{5, 4};
	QTableView view;
	ColumnFitter* fitter = nullptr;

	explicit Fixture(int width, const QList<double>& weights = {3, 2, 1, 1},
		const QList<int>& minimums = {100, 80, 50, 50})
	{
		model.setHorizontalHeaderLabels({"A", "B", "C", "D"});
		view.setModel(&model);
		fitter = new ColumnFitter(&view, weights, minimums, &view);
		view.resize(width, 300);
		view.show();
		QVERIFY(QTest::qWaitForWindowExposed(&view));
		fitter->fit();
	}

	int available() const { return view.viewport()->width(); }
	int width(int column) const { return view.horizontalHeader()->sectionSize(column); }
};

} // namespace

class TestColumnFitter : public QObject
{
	Q_OBJECT

private slots:
	// ---- distribute(): the arithmetic ----

	void distributeByWeight()
	{
		const QList<int> widths = ColumnFitter::distribute({3, 1}, {0, 0}, 400);
		QCOMPARE(widths, QList<int>({300, 100}));
	}

	void distributeAddsUpExactly()
	{
		// 1000 / 3 does not divide evenly; the leftover pixel must not be lost.
		const QList<int> widths = ColumnFitter::distribute({1, 1, 1}, {0, 0, 0}, 1000);
		QCOMPARE(sum(widths), 1000);
		for (int width : widths)
			QVERIFY(width == 333 || width == 334);
	}

	void distributeRespectsMinimums()
	{
		// By weight the second column would get 50; its minimum is 120, so it
		// gets 120 and the first column takes the rest.
		const QList<int> widths = ColumnFitter::distribute({9, 1}, {0, 120}, 500);
		QCOMPARE(widths, QList<int>({380, 120}));
	}

	void distributeCascadingMinimums()
	{
		// Pinning one column at its minimum shrinks the others' shares enough
		// to push a second one under its minimum too.
		const QList<int> widths = ColumnFitter::distribute({10, 1, 2}, {0, 100, 100}, 400);
		QCOMPARE(widths.at(1), 100);
		QCOMPARE(widths.at(2), 100);
		QCOMPARE(widths.at(0), 200);
		QCOMPARE(sum(widths), 400);
	}

	void distributeAllAtMinimumWhenTooNarrow()
	{
		const QList<int> widths = ColumnFitter::distribute({1, 1, 1}, {100, 80, 60}, 150);
		QCOMPARE(widths, QList<int>({100, 80, 60}));
	}

	void distributeEmpty()
	{
		QVERIFY(ColumnFitter::distribute({}, {}, 500).isEmpty());
	}

	// ---- On a real table ----

	void columnsFillTheTable()
	{
		Fixture f(800);
		QCOMPARE(visibleWidth(f.view), f.available());
		// In proportion 3:2:1:1 (within rounding).
		QVERIFY(qAbs(f.width(0) - 3 * f.width(2)) <= 3);
		QVERIFY(qAbs(f.width(1) - 2 * f.width(2)) <= 3);
	}

	void columnsFollowTheWindow()
	{
		Fixture f(800);
		const int before = f.width(0);
		f.view.resize(1200, 300);
		QTRY_COMPARE(visibleWidth(f.view), f.available());
		QVERIFY(f.width(0) > before);

		f.view.resize(600, 300);
		QTRY_COMPARE(visibleWidth(f.view), f.available());
		QVERIFY(f.width(0) < before);
	}

	void columnsStopAtMinimum()
	{
		Fixture f(800);
		f.view.resize(200, 300);
		QTRY_VERIFY(f.width(0) == f.fitter->minimumWidth(0));
		for (int column = 0; column < 4; ++column)
			QCOMPARE(f.width(column), f.fitter->minimumWidth(column));
		// Wider than the table, so it scrolls sideways instead.
		QVERIFY(visibleWidth(f.view) > f.available());
	}

	void minimumIncludesHeaderText()
	{
		Fixture f(800, {1, 1, 1, 1}, {0, 0, 0, 0});
		f.model.setHorizontalHeaderLabels({"A", "A much longer column heading", "C", "D"});
		const int needed = f.view.horizontalHeader()->sectionSizeHint(1);
		QVERIFY(f.fitter->minimumWidth(1) >= needed);
	}

	void hidingAColumnRefits()
	{
		Fixture f(800);
		f.view.horizontalHeader()->setSectionHidden(1, true);
		f.fitter->fit();
		QCOMPARE(visibleWidth(f.view), f.available());

		f.view.horizontalHeader()->setSectionHidden(1, false);
		f.fitter->fit();
		QCOMPARE(visibleWidth(f.view), f.available());
		QVERIFY(f.width(1) > 0);
	}

	void showingAColumnIsNotTakenAsADrag()
	{
		Fixture f(800);
		f.view.horizontalHeader()->setSectionHidden(3, true);
		f.fitter->fit();
		QSignalSpy spy(f.fitter, &ColumnFitter::weightsChanged);
		f.view.horizontalHeader()->setSectionHidden(3, false);
		QCOMPARE(spy.count(), 0);
	}

	void draggingKeepsTheDraggedWidth()
	{
		Fixture f(800);
		QSignalSpy spy(f.fitter, &ColumnFitter::weightsChanged);
		// What the header does when the user drags column 2's edge.
		f.view.horizontalHeader()->resizeSection(2, 250);
		QCOMPARE(f.width(2), 250);
		QCOMPARE(visibleWidth(f.view), f.available());
		QCOMPARE(spy.count(), 1);

		// The new proportions stick when the window is resized.
		const double ratio = double(f.width(2)) / f.available();
		f.view.resize(1100, 300);
		QTRY_COMPARE(visibleWidth(f.view), f.available());
		QVERIFY(qAbs(double(f.width(2)) / f.available() - ratio) < 0.02);
	}

	void draggingCannotSqueezeOthersBelowMinimum()
	{
		Fixture f(800);
		f.view.horizontalHeader()->resizeSection(0, 5000);
		QCOMPARE(visibleWidth(f.view), f.available());
		for (int column = 1; column < 4; ++column)
			QCOMPARE(f.width(column), f.fitter->minimumWidth(column));
	}

	void setWeightsValidates()
	{
		Fixture f(800);
		const QList<double> before = f.fitter->weights();
		f.fitter->setWeights({1, 2});         // wrong count
		QCOMPARE(f.fitter->weights(), before);
		f.fitter->setWeights({1, 0, 1, 1});   // a zero weight
		QCOMPARE(f.fitter->weights(), before);
		f.fitter->setWeights({1, 1, 1, 1});
		QCOMPARE(f.fitter->weights(), QList<double>({1, 1, 1, 1}));
		QVERIFY(qAbs(f.width(0) - f.width(1)) <= 1);
	}
};

QTEST_MAIN(TestColumnFitter)
#include "tst_column_fitter.moc"
