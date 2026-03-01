#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>

class EmptyStateWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EmptyStateWidget(const QString& iconPath,
                              const QString& title,
                              const QString& description,
                              const QString& ctaText = QString(),
                              QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setDescription(const QString& description);
    void setIcon(const QString& iconPath);

signals:
    void actionClicked();

private:
    QLabel* m_iconLabel;
    QLabel* m_titleLabel;
    QLabel* m_descLabel;
    QPushButton* m_ctaButton;
};
