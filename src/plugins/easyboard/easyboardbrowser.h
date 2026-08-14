// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QWidget>
#include <qabstractitemmodel.h>

QT_FORWARD_DECLARE_CLASS(QLabel)

namespace Utils::StyleHelper {
class TextFormat;
}

namespace EasyBoard::Internal {

class EasyBoardBrowser final : public QWidget
{
    Q_OBJECT

public:
    EasyBoardBrowser(QWidget *parent = nullptr);
    ~EasyBoardBrowser();

    void setFilter(const QString &filter);

    void adjustToWidth(const int width);
    QSize sizeHint() const override;

    int extraListViewWidth() const; // Space for scrollbar, etc.

    void showEvent(QShowEvent *event) override;
    void editBoardValue(const QModelIndex &idx);

    void newBoard();

    void removeFromList(const QModelIndex &idx);
    void setDefault(const QModelIndex &idx);

    void fetchBoards(const QModelIndex &idx);

    void processPendingDatagrams(QJsonObject str);
    void udpConnectOut();
    void udpConnectUpdate(QJsonObject);
signals:
    void itemSelected(const QModelIndex &current, const QModelIndex &previous);
    void itemChanged(const QModelIndex &current, const QModelIndex &previous);

private:
    class EasyBoardBrowserPrivate *d = nullptr;
    QTimer timer;
    QModelIndex m_modelIndex;
};

QLabel *tfLabel(const Utils::StyleHelper::TextFormat &tf, bool singleLine = true);

constexpr static QSize iconBgSizeSmall{50, 50};
constexpr static QSize iconBgSizeBig{68, 68};

constexpr static QSize imgBgSize{280, 180};

enum Size {
    SizeSmall,
    SizeBig,
    SizeImge,
};
QPixmap itemIcon(const QModelIndex &index, Size size);
QPixmap boardIcon(const QModelIndex &index, Size size);
} // namespace EasyBoard::Internal
