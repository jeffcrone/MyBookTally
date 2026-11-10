#include "ColumnFitter.h"

#include <QEvent>
#include <QHeaderView>
#include <QScrollBar>
#include <QTableView>

#include <algorithm>
#include <cmath>

namespace mbt {

ColumnFitter::ColumnFitter(QTableView* view, const QList<double>& weights, const QList<int>& minimums,
	QObject* parent)
	: QObject(parent)
	, m_view(view)
	, m_header(view->horizontalHeader())
	, m_weights(weights)
	, m_minimums(minimums)
{
	const int count = m_header->count();
	while (m_weights.size() < count)
		m_weights.append(1.0);
	while (m_minimums.size() < count)
		m_minimums.append(0);

	// Interactive: the program sets widths, and the user can still drag.
	// The last section must not stretch on its own, or it would fight the
	// widths fit() works out.
	m_header->setSectionResizeMode(QHeaderView::Interactive);
	m_header->setStretchLastSection(false);
	// fit() always fills the width exactly, so a sideways scroll bar only
	// appears when the minimums do not fit, which is when it is needed.
	m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

	// The viewport is the area inside the frame and scroll bars; it is
	// resized when the window is, and also when the vertical scroll bar
	// appears or disappears, which changes the width available too.
	m_view->viewport()->installEventFilter(this);
	connect(m_header, &QHeaderView::sectionResized, this, &ColumnFitter::onSectionResized);
	// Showing or hiding a column changes the section count only for some
	// operations, so MainWindow also calls fit() itself after toggling one.
	connect(m_header, &QHeaderView::sectionCountChanged, this, [this] { fit(); });
	connect(m_header, &QHeaderView::sectionMoved, this, [this] { fit(); });
}

void ColumnFitter::setWeights(const QList<double>& weights)
{
	if (weights.size() != m_header->count())
		return;
	if (std::any_of(weights.begin(), weights.end(), [](double w) { return !(w > 0); }))
		return;
	m_weights = weights;
	fit();
}

int ColumnFitter::minimumWidth(int column) const
{
	// Wide enough for the header's text and sort arrow, so a column is
	// never squeezed until its own title is cut off.
	const int headerNeeds = m_header->sectionSizeHint(column);
	const int configured = column < m_minimums.size() ? m_minimums.at(column) : 0;
	return std::max({configured, headerNeeds, m_header->minimumSectionSize()});
}

QList<int> ColumnFitter::visibleColumns() const
{
	QList<int> columns;
	for (int visual = 0; visual < m_header->count(); ++visual) {
		const int logical = m_header->logicalIndex(visual);
		if (!m_header->isSectionHidden(logical))
			columns.append(logical);
	}
	return columns;
}

QList<int> ColumnFitter::distribute(const QList<double>& weights, const QList<int>& minimums, int available)
{
	const int count = int(weights.size());
	QList<int> widths(count, 0);
	if (count == 0)
		return widths;

	// Repeatedly share the space by weight; any column whose share falls
	// short of its minimum is pinned at the minimum and taken out of the
	// sharing, which leaves less for the others, so check again until no
	// more columns need pinning.
	QList<bool> pinned(count, false);
	int remaining = available;
	bool changed = true;
	while (changed) {
		changed = false;
		double totalWeight = 0;
		for (int i = 0; i < count; ++i) {
			if (!pinned[i])
				totalWeight += weights[i];
		}
		for (int i = 0; i < count; ++i) {
			if (pinned[i])
				continue;
			const double share = totalWeight > 0 ? remaining * weights[i] / totalWeight : 0;
			if (share < minimums[i]) {
				pinned[i] = true;
				widths[i] = minimums[i];
				remaining -= minimums[i];
				changed = true;
				break; // restart with the smaller pool
			}
		}
	}

	// Share what is left among the unpinned columns. Rounding down can
	// leave a few pixels over; give them to the last unpinned column so
	// the total is exact and no sliver of empty header shows at the right.
	double totalWeight = 0;
	int lastFree = -1;
	for (int i = 0; i < count; ++i) {
		if (!pinned[i]) {
			totalWeight += weights[i];
			lastFree = i;
		}
	}
	int used = 0;
	for (int i = 0; i < count; ++i) {
		if (pinned[i])
			continue;
		widths[i] = int(std::floor(remaining * weights[i] / totalWeight));
		used += widths[i];
	}
	if (lastFree >= 0)
		widths[lastFree] += remaining - used;
	return widths;
}

void ColumnFitter::fit()
{
	if (m_fitting)
		return;
	const int available = m_view->viewport()->width();
	const QList<int> columns = visibleColumns();
	if (available <= 0 || columns.isEmpty())
		return;

	QList<double> weights;
	QList<int> minimums;
	for (int column : columns) {
		weights.append(m_weights.value(column, 1.0));
		minimums.append(minimumWidth(column));
	}
	const QList<int> widths = distribute(weights, minimums, available);

	m_fitting = true;
	for (int i = 0; i < columns.size(); ++i) {
		if (m_header->sectionSize(columns[i]) != widths[i])
			m_header->resizeSection(columns[i], widths[i]);
	}
	m_fitting = false;
}

void ColumnFitter::onSectionResized(int column, int oldSize, int newSize)
{
	// Ignore resizes that are not the user dragging an edge: fit()'s own,
	// a column being hidden (new size 0), and a column being shown (old
	// size 0). Showing and hiding are followed by a fit() instead.
	if (m_fitting || m_header->isSectionHidden(column) || newSize <= 0 || oldSize <= 0)
		return;

	// The user is dragging this column's edge. Keep it at the width they
	// chose - but never so wide that the other columns cannot fit at their
	// minimums - and share the rest among the others by their weights.
	const int available = m_view->viewport()->width();
	QList<int> others;
	QList<double> otherWeights;
	QList<int> otherMinimums;
	int othersNeed = 0;
	for (int c : visibleColumns()) {
		if (c == column)
			continue;
		others.append(c);
		otherWeights.append(m_weights.value(c, 1.0));
		otherMinimums.append(minimumWidth(c));
		othersNeed += minimumWidth(c);
	}
	const int dragged = std::max(minimumWidth(column), std::min(newSize, available - othersNeed));
	const QList<int> widths = distribute(otherWeights, otherMinimums, available - dragged);

	m_fitting = true;
	m_header->resizeSection(column, dragged);
	for (int i = 0; i < others.size(); ++i)
		m_header->resizeSection(others[i], widths[i]);
	m_fitting = false;

	// From now on the proportions are the ones the user arranged. Hidden
	// columns keep their old weights for when they are shown again; the
	// visible ones are rescaled to the same total so the two stay comparable.
	double oldTotal = m_weights.value(column, 1.0);
	for (double w : otherWeights)
		oldTotal += w;
	const double scale = available > 0 ? oldTotal / available : 1.0;
	m_weights[column] = dragged * scale;
	for (int i = 0; i < others.size(); ++i)
		m_weights[others[i]] = std::max(widths[i], 1) * scale;
	emit weightsChanged();
}

bool ColumnFitter::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_view->viewport() && event->type() == QEvent::Resize)
		fit();
	return QObject::eventFilter(watched, event);
}

} // namespace mbt
