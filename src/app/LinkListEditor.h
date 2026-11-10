#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QComboBox;
class QListWidget;
class QPushButton;

namespace mbt {

// One linked record as the editor sees it. id is empty for a name the user
// typed that does not exist yet; the book editor creates it on save.
struct LinkChoice
{
	QString id;
	QString name;

	bool operator==(const LinkChoice& other) const
	{
		return id == other.id && name == other.name;
	}
};

// Picks a set of linked records for a book: its authors, or its genres.
//
// An editable drop-down offers the existing records and also accepts a new
// name; "Add" puts the choice in the list below. With ordered set, Up and
// Down buttons let the user arrange the list (author credit order matters;
// genre order does not).
class LinkListEditor : public QWidget
{
	Q_OBJECT

public:
	LinkListEditor(bool ordered, const QString& placeholder, QWidget* parent = nullptr);

	// The records offered in the drop-down.
	void setChoices(const QList<LinkChoice>& choices);
	void setSelected(const QList<LinkChoice>& selected);
	QList<LinkChoice> selected() const;

	// Adds whatever is typed in the drop-down, as the Add button does.
	// Returns false if the text was empty or already in the list.
	bool addCurrentText();

	QComboBox* picker() const { return m_picker; }
	QListWidget* list() const { return m_list; }

private:
	void appendChoice(const LinkChoice& choice);
	void moveCurrent(int delta);
	void updateButtons();

	bool m_ordered;
	QList<LinkChoice> m_choices;
	QComboBox* m_picker;
	QListWidget* m_list;
	QPushButton* m_add;
	QPushButton* m_remove;
	QPushButton* m_up = nullptr;
	QPushButton* m_down = nullptr;
};

} // namespace mbt
