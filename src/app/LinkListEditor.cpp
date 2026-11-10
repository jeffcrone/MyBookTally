#include "LinkListEditor.h"

#include <QComboBox>
#include <QCompleter>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace mbt {

namespace {

constexpr int IdRole = Qt::UserRole;
// The bare name; the visible text of a new record has " (new)" after it.
constexpr int NameRole = Qt::UserRole + 1;

} // namespace

LinkListEditor::LinkListEditor(bool ordered, const QString& placeholder, QWidget* parent)
	: QWidget(parent)
	, m_ordered(ordered)
	, m_picker(new QComboBox(this))
	, m_list(new QListWidget(this))
	, m_add(new QPushButton(tr("Add"), this))
	, m_remove(new QPushButton(tr("Remove"), this))
{
	m_picker->setEditable(true);
	// Typing must not silently add new entries to the drop-down itself;
	// only the Add button commits a choice.
	m_picker->setInsertPolicy(QComboBox::NoInsert);
	m_picker->lineEdit()->setPlaceholderText(placeholder);
	m_picker->completer()->setCaseSensitivity(Qt::CaseInsensitive);
	m_picker->completer()->setFilterMode(Qt::MatchContains);
	m_picker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	auto* pickRow = new QHBoxLayout;
	pickRow->addWidget(m_picker, 1);
	pickRow->addWidget(m_add);

	auto* buttons = new QVBoxLayout;
	buttons->addWidget(m_remove);
	if (m_ordered) {
		m_up = new QPushButton(tr("Up"), this);
		m_down = new QPushButton(tr("Down"), this);
		buttons->addWidget(m_up);
		buttons->addWidget(m_down);
		connect(m_up, &QPushButton::clicked, this, [this] { moveCurrent(-1); });
		connect(m_down, &QPushButton::clicked, this, [this] { moveCurrent(1); });
	}
	buttons->addStretch();

	auto* listRow = new QHBoxLayout;
	listRow->addWidget(m_list, 1);
	listRow->addLayout(buttons);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addLayout(pickRow);
	layout->addLayout(listRow);

	m_list->setMaximumHeight(120);

	// Only the button adds. Enter in the drop-down is left alone on purpose:
	// inside a dialog it also presses the dialog's OK button, and doing both
	// at once would be surprising.
	connect(m_add, &QPushButton::clicked, this, [this] { addCurrentText(); });
	connect(m_remove, &QPushButton::clicked, this, [this] {
		delete m_list->currentItem();
		updateButtons();
	});
	connect(m_list, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
	updateButtons();
}

void LinkListEditor::setChoices(const QList<LinkChoice>& choices)
{
	m_choices = choices;
	m_picker->clear();
	for (const LinkChoice& choice : choices)
		m_picker->addItem(choice.name, choice.id);
	m_picker->setCurrentIndex(-1);
	m_picker->clearEditText();
}

void LinkListEditor::setSelected(const QList<LinkChoice>& selected)
{
	m_list->clear();
	for (const LinkChoice& choice : selected)
		appendChoice(choice);
	updateButtons();
}

QList<LinkChoice> LinkListEditor::selected() const
{
	QList<LinkChoice> result;
	for (int row = 0; row < m_list->count(); ++row) {
		const QListWidgetItem* item = m_list->item(row);
		result.append({item->data(IdRole).toString(), item->data(NameRole).toString()});
	}
	return result;
}

bool LinkListEditor::addCurrentText()
{
	const QString name = m_picker->currentText().trimmed();
	if (name.isEmpty())
		return false;

	// Typed text that matches an existing record (ignoring case) means that
	// record, not a new one with the same name.
	LinkChoice choice{QString(), name};
	for (const LinkChoice& existing : m_choices) {
		if (existing.name.compare(name, Qt::CaseInsensitive) == 0) {
			choice = existing;
			break;
		}
	}

	for (const LinkChoice& already : selected()) {
		const bool sameRecord = !choice.id.isEmpty() && already.id == choice.id;
		const bool sameNewName = choice.id.isEmpty() && already.id.isEmpty()
			&& already.name.compare(choice.name, Qt::CaseInsensitive) == 0;
		if (sameRecord || sameNewName)
			return false;
	}

	appendChoice(choice);
	m_picker->setCurrentIndex(-1);
	m_picker->clearEditText();
	updateButtons();
	return true;
}

void LinkListEditor::appendChoice(const LinkChoice& choice)
{
	auto* item = new QListWidgetItem(choice.id.isEmpty()
		? tr("%1 (new)").arg(choice.name)
		: choice.name);
	item->setData(IdRole, choice.id);
	item->setData(NameRole, choice.name);
	m_list->addItem(item);
}

void LinkListEditor::moveCurrent(int delta)
{
	const int row = m_list->currentRow();
	const int target = row + delta;
	if (row < 0 || target < 0 || target >= m_list->count())
		return;
	QListWidgetItem* item = m_list->takeItem(row);
	m_list->insertItem(target, item);
	m_list->setCurrentRow(target);
}

void LinkListEditor::updateButtons()
{
	const int row = m_list->currentRow();
	m_remove->setEnabled(row >= 0);
	if (m_up)
		m_up->setEnabled(row > 0);
	if (m_down)
		m_down->setEnabled(row >= 0 && row < m_list->count() - 1);
}

} // namespace mbt
