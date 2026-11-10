#pragma once

// The small single-purpose dialogs: editing a review, a rating, or a list's
// name, and choosing export options.

#include "Entities.h"
#include "JsonExchange.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QRadioButton;

namespace mbt {

class ReviewDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ReviewDialog(const Review& review, QWidget* parent = nullptr);
	// The review with the dialog's text and reviewer; other fields unchanged.
	Review review() const;
	void accept() override;

private:
	Review m_review;
	QPlainTextEdit* m_text;
	QLineEdit* m_reviewer;
};

class RatingDialog : public QDialog
{
	Q_OBJECT

public:
	explicit RatingDialog(const Rating& rating, QWidget* parent = nullptr);
	Rating rating() const;
	void accept() override;

private:
	Rating m_rating;
	QDoubleSpinBox* m_value;
	QDoubleSpinBox* m_scale;
	QLineEdit* m_reviewer;
};

class ListDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ListDialog(const BookList& list, QWidget* parent = nullptr);
	BookList list() const;
	void accept() override;

private:
	BookList m_list;
	QLineEdit* m_name;
	QPlainTextEdit* m_description;
};

class ExportOptionsDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ExportOptionsDialog(int bookCount, QWidget* parent = nullptr);
	ExportOptions options() const;

private:
	QRadioButton* m_withIds;
	QRadioButton* m_withNames;
	QCheckBox* m_covers;
};

} // namespace mbt
