#include "SmallDialogs.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QVBoxLayout>

namespace mbt {

namespace {

QDialogButtonBox* addOkCancel(QDialog* dialog, QBoxLayout* layout)
{
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	layout->addWidget(buttons);
	return buttons;
}

} // namespace

// ---- ReviewDialog ---------------------------------------------------------

ReviewDialog::ReviewDialog(const Review& review, QWidget* parent)
	: QDialog(parent)
	, m_review(review)
	, m_text(new QPlainTextEdit(review.text, this))
	, m_reviewer(new QLineEdit(review.reviewer, this))
{
	setWindowTitle(review.id.isEmpty() ? tr("Add Review") : tr("Edit Review"));
	m_reviewer->setPlaceholderText(tr("Leave blank if the review is yours"));

	auto* form = new QFormLayout;
	form->addRow(tr("&Review:"), m_text);
	form->addRow(tr("Review&er:"), m_reviewer);

	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	addOkCancel(this, layout);
	resize(480, 320);
}

Review ReviewDialog::review() const
{
	Review result = m_review;
	result.text = m_text->toPlainText().trimmed();
	result.reviewer = m_reviewer->text().trimmed();
	return result;
}

void ReviewDialog::accept()
{
	if (m_text->toPlainText().trimmed().isEmpty()) {
		QMessageBox::warning(this, windowTitle(), tr("Please write the review first."));
		m_text->setFocus();
		return;
	}
	QDialog::accept();
}

// ---- RatingDialog ---------------------------------------------------------

RatingDialog::RatingDialog(const Rating& rating, QWidget* parent)
	: QDialog(parent)
	, m_rating(rating)
	, m_value(new QDoubleSpinBox(this))
	, m_scale(new QDoubleSpinBox(this))
	, m_reviewer(new QLineEdit(rating.reviewer, this))
{
	setWindowTitle(rating.id.isEmpty() ? tr("Add Rating") : tr("Edit Rating"));

	// Half steps cover the common "3.5 stars". Any decimal can be typed.
	m_value->setDecimals(1);
	m_value->setSingleStep(0.5);
	m_value->setRange(0, 1000);
	m_scale->setDecimals(0);
	m_scale->setRange(1, 1000);
	m_scale->setValue(rating.scale > 0 ? rating.scale : 5);
	m_value->setValue(rating.value);
	m_value->setMaximum(m_scale->value());
	// Keep the score within what it is out of as the scale changes.
	connect(m_scale, &QDoubleSpinBox::valueChanged, m_value, &QDoubleSpinBox::setMaximum);

	m_reviewer->setPlaceholderText(tr("Leave blank if the rating is yours"));

	auto* scoreRow = new QHBoxLayout;
	scoreRow->addWidget(m_value);
	scoreRow->addWidget(new QLabel(tr("out of"), this));
	scoreRow->addWidget(m_scale);
	scoreRow->addStretch();

	auto* form = new QFormLayout;
	form->addRow(tr("&Score:"), scoreRow);
	form->addRow(tr("Review&er:"), m_reviewer);

	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	addOkCancel(this, layout);
}

Rating RatingDialog::rating() const
{
	Rating result = m_rating;
	result.value = m_value->value();
	result.scale = m_scale->value();
	result.reviewer = m_reviewer->text().trimmed();
	return result;
}

void RatingDialog::accept()
{
	// The spin boxes already enforce the range; nothing else to check.
	QDialog::accept();
}

// ---- ListDialog -----------------------------------------------------------

ListDialog::ListDialog(const BookList& list, QWidget* parent)
	: QDialog(parent)
	, m_list(list)
	, m_name(new QLineEdit(list.name, this))
	, m_description(new QPlainTextEdit(list.description, this))
{
	setWindowTitle(list.id.isEmpty() ? tr("New List") : tr("Edit List"));
	m_name->setPlaceholderText(tr("For example: Favorites"));

	auto* form = new QFormLayout;
	form->addRow(tr("&Name:"), m_name);
	form->addRow(tr("&Description:"), m_description);

	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	addOkCancel(this, layout);
	resize(420, 260);
}

BookList ListDialog::list() const
{
	BookList result = m_list;
	result.name = m_name->text().trimmed();
	result.description = m_description->toPlainText().trimmed();
	return result;
}

void ListDialog::accept()
{
	if (m_name->text().trimmed().isEmpty()) {
		QMessageBox::warning(this, windowTitle(), tr("Please give the list a name."));
		m_name->setFocus();
		return;
	}
	QDialog::accept();
}

// ---- ExportOptionsDialog --------------------------------------------------

ExportOptionsDialog::ExportOptionsDialog(int bookCount, QWidget* parent)
	: QDialog(parent)
	, m_withIds(new QRadioButton(tr("Include &IDs (best for backups and moving to another computer)"), this))
	, m_withNames(new QRadioButton(tr("&Names only (easier to read and to use in other programs)"), this))
	, m_covers(new QCheckBox(tr("Include &cover images (makes the file much larger)"), this))
{
	setWindowTitle(tr("Export Books"));
	m_withIds->setChecked(true);
	m_covers->setChecked(true);

	auto* linksBox = new QGroupBox(tr("Linked authors, series, genres, and lists"), this);
	auto* linksLayout = new QVBoxLayout(linksBox);
	linksLayout->addWidget(m_withIds);
	linksLayout->addWidget(m_withNames);

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("Exporting %n book(s).", nullptr, bookCount), this));
	layout->addWidget(linksBox);
	layout->addWidget(m_covers);
	addOkCancel(this, layout);
}

ExportOptions ExportOptionsDialog::options() const
{
	ExportOptions result;
	result.includeIds = m_withIds->isChecked();
	result.includeCovers = m_covers->isChecked();
	return result;
}

} // namespace mbt
