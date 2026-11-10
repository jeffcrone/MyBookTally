#include "UiUtil.h"

#include "DatabaseError.h"
#include "LookupRepository.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QIcon>
#include <QMessageBox>

namespace mbt {

bool runReportingErrors(QWidget* parent, const QString& title, const std::function<void()>& action)
{
	try {
		action();
		return true;
	} catch (const DatabaseError& error) {
		// The first line is the explanation; anything after it is the SQL,
		// useful when reporting a bug but noise in the main message.
		QMessageBox box(QMessageBox::Warning, title,
			error.message().section(QLatin1Char('\n'), 0, 0), QMessageBox::Ok, parent);
		const QString detail = error.message().section(QLatin1Char('\n'), 1).trimmed();
		if (!detail.isEmpty())
			box.setDetailedText(detail);
		box.exec();
		return false;
	}
}

void fillLookupCombo(QComboBox* combo, const Database& database, LookupKind kind,
	const QString& selectedId)
{
	combo->clear();
	combo->addItem(QCoreApplication::translate("mbt", "(none)"), QString());
	for (const LookupItem& item : LookupRepository(database, kind).all())
		combo->addItem(item.name, item.id);
	const int index = combo->findData(selectedId);
	combo->setCurrentIndex(index < 0 ? 0 : index);
}

QString comboId(const QComboBox* combo)
{
	return combo->currentData().toString();
}

QIcon appIcon()
{
	QIcon icon;
	for (int size : {16, 24, 32, 48, 64, 128, 256})
		icon.addFile(QStringLiteral(":/icons/MyBookTally_Icon_%1x%1.png").arg(size), QSize(size, size));
	return icon;
}

} // namespace mbt
