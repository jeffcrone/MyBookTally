#pragma once

#include <QList>
#include <QObject>

class QHeaderView;
class QTableView;

namespace mbt {

// Keeps a table's visible columns exactly as wide, together, as the table.
//
// Each column has a weight. The table's width is shared out in proportion
// to the weights of the columns that are showing, so when the window grows
// or shrinks every column grows or shrinks with it, and showing or hiding a
// column redistributes the space instead of leaving a gap or pushing columns
// off the edge.
//
// Each column also has a minimum width. A column whose share would be below
// its minimum gets the minimum instead, and the rest share what is left. If
// the minimums alone are wider than the table, every column sits at its
// minimum and the table scrolls sideways.
//
// Dragging a column edge still works: the dragged column keeps the width the
// user gave it and the other columns share the remaining space. The new
// widths become the weights from then on, so the user's proportions are
// kept as the window is resized later.
//
// Qt's QHeaderView::Stretch mode was not used because it gives every column
// the same width (a Pages column as wide as Title) and stops the user from
// dragging columns at all.
class ColumnFitter : public QObject
{
	Q_OBJECT

public:
	// Takes over sizing view's horizontal header. weights and minimums are
	// per logical column; columns beyond the lists get weight 1 and the
	// header-text width as their minimum.
	ColumnFitter(QTableView* view, const QList<double>& weights, const QList<int>& minimums,
		QObject* parent = nullptr);

	// Current weights, for saving in the settings.
	const QList<double>& weights() const { return m_weights; }
	// Restores saved weights. Ignored if the list does not have one positive
	// weight per column (a saved list from a version with different columns).
	void setWeights(const QList<double>& weights);

	// The minimum width actually applied to a column: the larger of the
	// configured minimum and what its header text needs.
	int minimumWidth(int column) const;

	// Shares the table's current width out among the visible columns. Called
	// automatically when the table is resized or a column is shown or
	// hidden; call it after anything else that changes the columns.
	void fit();

	// The arithmetic behind fit(), kept separate so it can be tested on its
	// own: shares available pixels among columns in proportion to weights,
	// giving each at least its minimum. The result always adds up to exactly
	// available unless the minimums alone exceed it.
	static QList<int> distribute(const QList<double>& weights, const QList<int>& minimums, int available);

signals:
	// The user dragged a column edge and the weights changed.
	void weightsChanged();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void onSectionResized(int column, int oldSize, int newSize);
	// Visible columns in left-to-right (visual) order.
	QList<int> visibleColumns() const;

	QTableView* m_view;
	QHeaderView* m_header;
	QList<double> m_weights;
	QList<int> m_minimums;
	// True while fit() itself is resizing columns, so those resizes are not
	// mistaken for the user dragging an edge.
	bool m_fitting = false;
};

} // namespace mbt
