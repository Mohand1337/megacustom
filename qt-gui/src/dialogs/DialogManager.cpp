// DialogManager.cpp - Centralized dialog lifecycle management implementation
#include "DialogManager.h"
#include <QDebug>

namespace MegaCustom {

DialogManager& DialogManager::instance()
{
    static DialogManager instance;
    return instance;
}

DialogManager::DialogManager(QObject* parent)
    : QObject(parent)
{
}

void DialogManager::registerDialog(const QString& typeName, QDialog* dialog)
{
    m_dialogs[typeName] = dialog;
    qDebug() << "DialogManager: Registered dialog" << typeName;
}

void DialogManager::unregisterDialog(const QString& typeName)
{
    if (m_dialogs.remove(typeName)) {
        qDebug() << "DialogManager: Unregistered dialog" << typeName;
        emit dialogClosed(typeName);

        if (m_dialogs.isEmpty()) {
            emit allDialogsClosed();
        }
    }
}

void DialogManager::closeAllDialogs()
{
    // Copy keys to avoid modification during iteration
    QStringList typeNames = m_dialogs.keys();

    for (const QString& typeName : typeNames) {
        auto it = m_dialogs.find(typeName);
        if (it != m_dialogs.end() && !it.value().isNull()) {
            QDialog* dialog = it.value().data();
            dialog->close();
        }
    }

    // Clear any remaining null pointers
    m_dialogs.clear();

    emit allDialogsClosed();
}

int DialogManager::openDialogCount() const
{
    int count = 0;
    for (auto it = m_dialogs.constBegin(); it != m_dialogs.constEnd(); ++it) {
        if (!it.value().isNull()) {
            ++count;
        }
    }
    return count;
}

QStringList DialogManager::openDialogTypeNames() const
{
    QStringList names;
    for (auto it = m_dialogs.constBegin(); it != m_dialogs.constEnd(); ++it) {
        if (!it.value().isNull()) {
            names.append(it.key());
        }
    }
    return names;
}

} // namespace MegaCustom
