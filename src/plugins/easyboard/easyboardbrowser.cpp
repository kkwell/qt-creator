// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "easyboardbrowser.h"

#include "easyboardtr.h"
#include "easyboardmodel.h"
#include "newboarddialog.h"
#include "ssdp/netproperty.h"

#ifdef WITH_TESTS
#include "extensionmanager_test.h"
#endif // WITH_TESTS

#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/welcomepagehelper.h>

#include <solutions/spinner/spinner.h>
#include <solutions/tasking/networkquery.h>
#include <solutions/tasking/tasktree.h>
#include <solutions/tasking/tasktreerunner.h>

#include <utils/algorithm.h>
#include <utils/elidinglabel.h>
#include <utils/fancylineedit.h>
#include <utils/hostosinfo.h>
#include <utils/icon.h>
#include <utils/layoutbuilder.h>
#include <utils/networkaccessmanager.h>
#include <utils/stylehelper.h>

#include <QApplication>
#include <QItemDelegate>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QLoggingCategory>
#include <QHelpEvent>
#include <QMenu>

using namespace Core;
using namespace Utils;
using namespace StyleHelper;
using namespace SpacingTokens;
using namespace WelcomePageHelpers;

namespace EasyBoard::Internal {

Q_LOGGING_CATEGORY(browserLog, "qtc.easyboard.browser", QtWarningMsg)

constexpr int gapSize = HGapL;
constexpr int itemWidth = 330;
constexpr int cellWidth = itemWidth + gapSize;

class OptionChooser : public QComboBox
{
public:
    OptionChooser(const FilePath &iconMask, const QString &textTemplate, QWidget *parent = nullptr)
        : QComboBox(parent)
        , m_iconDefault(Icon({{iconMask, m_colorDefault}}, Icon::Tint).icon())
        , m_iconActive(Icon({{iconMask, m_colorActive}}, Icon::Tint).icon())
        , m_textTemplate(textTemplate)
    {
        setMouseTracking(true);
        connect(this, &QComboBox::currentIndexChanged, this, &QWidget::updateGeometry);
    }

protected:
    void paintEvent([[maybe_unused]] QPaintEvent *event) override
    {
        // +------------+------+---------+---------------+------------+
        // |            |      |         |  (VPaddingXs) |            |
        // |            |      |         +---------------+            |
        // |(HPaddingXs)|(icon)|(HGapXxs)|<template%item>|(HPaddingXs)|
        // |            |      |         +---------------+            |
        // |            |      |         |  (VPaddingXs) |            |
        // +------------+------+---------+---------------+------------+

        const bool active = currentIndex() > 0;
        const bool hover = underMouse();
        const TextFormat &tF = (active || hover) ? m_itemActiveTf : m_itemDefaultTf;

        const QRect iconRect(HPaddingXs, 0, m_iconSize.width(), height());
        const int textX = iconRect.right() + 1 + HGapXxs;
        const QRect textRect(textX, VPaddingXs,
                             width() - HPaddingXs - textX, tF.lineHeight());

        QPainter p(this);
        (active ? m_iconActive : m_iconDefault).paint(&p, iconRect);
        p.setPen(tF.color());
        p.setFont(tF.font());
        const QString elidedText = p.fontMetrics().elidedText(currentFormattedText(),
                                                              Qt::ElideRight,
                                                              textRect.width() + HPaddingXs);
        p.drawText(textRect, tF.drawTextFlags, elidedText);
    }

    void enterEvent(QEnterEvent *event) override
    {
        QComboBox::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        QComboBox::leaveEvent(event);
        update();
    }

private:
    QSize sizeHint() const override
    {
        const QFontMetrics fm(m_itemDefaultTf.font());
        const int textWidth = fm.horizontalAdvance(currentFormattedText());
        const int width =
            HPaddingXs
            + m_iconSize.width()
            + HGapXxs
            + textWidth
            + HPaddingXs;
        const int height =
            VPaddingXs
            + m_itemDefaultTf.lineHeight()
            + VPaddingXs;
        return {width, height};
    }

    QString currentFormattedText() const
    {
        return m_textTemplate.arg(currentText());
    }

    constexpr static Theme::Color m_colorDefault = Theme::Token_Text_Muted;
    constexpr static Theme::Color m_colorActive = Theme::Token_Text_Default;
    constexpr static QSize m_iconSize{16, 16};
    constexpr static TextFormat m_itemDefaultTf
        {m_colorDefault, UiElement::UiElementLabelMedium};
    constexpr static TextFormat m_itemActiveTf
        {m_colorActive, m_itemDefaultTf.uiElement};
    const QIcon m_iconDefault;
    const QIcon m_iconActive;
    const QString m_textTemplate;
};

static QString boardStateDisplayString(BoardState state)
{
    switch (state) {
    case Online:
        return Tr::tr("Online");
    case Offline:
        return Tr::tr("Offline");
    default:
        return {};
    }
    return {};
}

class BoardItemDelegate : public QItemDelegate
{
    Q_OBJECT
public:
    constexpr static QSize dividerS{1, 16};
    constexpr static TextFormat itemNameTF
        {Theme::Token_Text_Default, UiElement::UiElementH6};
    constexpr static TextFormat countTF
        {Theme::Token_Text_Default, UiElement::UiElementLabelSmall,
         Qt::AlignCenter | Qt::TextDontClip};
    constexpr static TextFormat vendorTF
        {Theme::Token_Text_Muted, UiElement::UiElementLabelSmall,
         Qt::AlignVCenter | Qt::TextDontClip};
    constexpr static TextFormat stateTF
        {vendorTF.themeColor, UiElement::UiElementCaption, vendorTF.drawTextFlags};
    constexpr static TextFormat tagsTF
        {Theme::Token_Text_Default, UiElement::UiElementCaption};

    explicit BoardItemDelegate(QObject *parent = nullptr)
        : QItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index)
        const override
    {
        // +---------------+-------+---------------+----------------------------------------------------------------------+---------------+---------+
        // |               |       |               |                            (ExPaddingGapL)                           |               |         |
        // |               |       |               +-----------------------------+---------+--------+---------+-----------+               |         |
        // |               |       |               |          <itemName>         |(HGapXxs)|<status>|(HGapXxs)|<checkmark>|               |         |
        // |               |       |               +-----------------------------+---------+--------+---------+-----------+               |         |
        // |               |       |               |                               (VGapXxs)                              |               |         |
        // |               |       |               +--------+--------+--------------+--------+--------+---------+---------+               |         |
        // |(ExPaddingGapL)|<icon> |(ExPaddingGapL)|<vendor>|(HGapXs)|<divider>(h16)|(HGapXs)|<dlIcon>|(HGapXxs)|<dlCount>|(ExPaddingGapL)|(gapSize)|
        // |               |(50x50)|               +--------+--------+--------------+--------+--------+---------+---------+               |         |
        // |               |       |               |                               (VGapXxs)                              |               |         |
        // |               |       |               +----------------------------------------------------------------------+               |         |
        // |               |       |               |                                <tags>                                |               |         |
        // |               |       |               +----------------------------------------------------------------------+               |         |
        // |               |       |               |                            (ExPaddingGapL)                           |               |         |
        // +---------------+-------+---------------+----------------------------------------------------------------------+---------------+---------+
        // |                                                                (gapSize)                                                               |
        // +----------------------------------------------------------------------------------------------------------------------------------------+

        const QRect bgRGlobal = option.rect.adjusted(0, 0, -gapSize, -gapSize);
        const QRect bgR = bgRGlobal.translated(-option.rect.topLeft());

        const int middleColumnW = bgR.width() - ExPaddingGapL - iconBgSizeSmall.width()
                - ExPaddingGapL - ExPaddingGapL;

        int x = bgR.x();
        int y = bgR.y();
        x += ExPaddingGapL;
        const QRect iconBgR(x, y + (bgR.height() - iconBgSizeSmall.height()) / 2,
                            iconBgSizeSmall.width(), iconBgSizeSmall.height());
        x += iconBgSizeSmall.width() + ExPaddingGapL;
        y += ExPaddingGapL;
        const QRect itemNameR(x, y, middleColumnW, itemNameTF.lineHeight());
        const QString itemDisName = index.data(EasyBoardModel::RoleDisplayName).toString();

        const QString itemName = itemDisName.isEmpty()?index.data().toString():itemDisName;

        const QString itemVersion = index.data(EasyBoardModel::RoleVersion).toString();

        const QSize checkmarkS(12, 12);
        const QRect checkmarkR(x + middleColumnW - checkmarkS.width(), y,
                               checkmarkS.width(), checkmarkS.height());
        const BoardState state = index.data(EasyBoardModel::RoleBoardState).value<BoardState>();
        const QString stateString = boardStateDisplayString(state);
        const bool showState = (state == Online);
                // && !stateString.isEmpty();
        const QFont stateFont = stateTF.font();
        const QFontMetrics stateFM(stateFont);
        const int stateStringWidth = stateFM.horizontalAdvance(stateString);
        const QRect stateR(checkmarkR.x() - HGapXxs - stateStringWidth, y,
                           stateStringWidth, stateTF.lineHeight());

        y += itemNameR.height() + VGapXxs;
        const QRect vendorRowR(x, y, middleColumnW, vendorRowHeight());
        QRect vendorR = vendorRowR;

        y += vendorRowR.height() + VGapXxs;
        const QRect tagsR(x, y, middleColumnW, tagsTF.lineHeight());

        QTC_CHECK(option.rect.height() - 1 == tagsR.bottom() + ExPaddingGapL + gapSize);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(bgRGlobal.topLeft());

        // const bool isPack = index.data(RoleItemType) == ItemTypeLocal;
        {
            const bool selected = option.state & QStyle::State_Selected;
            const bool hovered = option.state & QStyle::State_MouseOver;
            const QColor fillColor =
                creatorColor(hovered ? WelcomePageHelpers::cardHoverBackground
                                              : WelcomePageHelpers::cardDefaultBackground);
            const QColor strokeColor =
                creatorColor(selected ? Theme::Token_Stroke_Strong
                                      : hovered ? WelcomePageHelpers::cardHoverStroke
                                                : WelcomePageHelpers::cardDefaultStroke);
            WelcomePageHelpers::drawCardBackground(painter, bgR, fillColor, strokeColor);
            // if(selected){
            //     QAbstractButton *installButton = new Button(Tr::tr("Default"), Button::MediumPrimary);
            //     installButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
            //     installButton->setVisible(true);
            //     if (installButton->isVisible())
            //         installButton->setToolTip(Tr::tr("Set As Default"));
            // }
        }
        {
            const QPixmap icon = itemIcon(index, SizeSmall);
            painter->drawPixmap(iconBgR.topLeft(), icon);
        }

        {
            QRect effectiveR = itemNameR;
            if (showState)
                effectiveR.setRight(stateR.left() - HGapXxs - 1);
            painter->setPen(itemNameTF.color());
            painter->setFont(itemNameTF.font());
            const QString titleElided
                = painter->fontMetrics().elidedText(itemName, Qt::ElideRight, effectiveR.width());
            painter->drawText(effectiveR, itemNameTF.drawTextFlags, titleElided);
        }

        if (index.data(EasyBoardModel::RoleDefault).toBool()) {
            static const QIcon checkmark = Icon({{":/easyboard/images/checkmark.png",
                                                  stateTF.themeColor}}, Icon::Tint).icon();
            checkmark.paint(painter, checkmarkR);
        }

        if(state == Online){
            painter->setPen(qRgb(156, 219, 166));//qRgb(239, 90, 111),stateTF.color()
            painter->setFont(stateTF.font());
            painter->drawText(stateR, stateTF.drawTextFlags, stateString);
        }
        {
            const QString vendor = index.data(EasyBoardModel::RoleDate).toString();
            const QFontMetrics fm(vendorTF.font());
            painter->setPen(vendorTF.color());
            painter->setFont(vendorTF.font());

            const QString vendorElided = fm.elidedText(vendor, Qt::ElideRight, vendorR.width());
            painter->drawText(vendorR, vendorTF.drawTextFlags, vendorElided);
        }
        {
            const QString tags = index.data(EasyBoardModel::RoleIp).toString();

            painter->setPen(tagsTF.color());
            painter->setFont(tagsTF.font());
            const QString tagsElided
                = painter->fontMetrics().elidedText(tags, Qt::ElideRight, tagsR.width());
            painter->drawText(tagsR, tagsTF.drawTextFlags, tagsElided);
            const QString verElided
                = painter->fontMetrics().elidedText(itemVersion, Qt::ElideRight, tagsR.width());
            painter->drawText(tagsR, tagsTF.drawTextFlags|Qt::AlignRight, verElided);

        }

        painter->restore();
    }

    static int vendorRowHeight()
    {
        return qMax(vendorTF.lineHeight(), dividerS.height());
    }

    QSize sizeHint([[maybe_unused]] const QStyleOptionViewItem &option,
                   [[maybe_unused]] const QModelIndex &index) const override
    {
        const int middleColumnH =
            itemNameTF.lineHeight()
            + VGapXxs
            + vendorRowHeight()
            + VGapXxs
            + tagsTF.lineHeight();
        const int height =
            ExPaddingGapL
            + qMax(iconBgSizeSmall.height(), middleColumnH)
            + ExPaddingGapL;
        return {cellWidth, height + gapSize};
    }

    bool editorEvent(QEvent *ev, QAbstractItemModel *model,
                     const QStyleOptionViewItem &, const QModelIndex &idx) final
    {
        if (ev->type() == QEvent::MouseButtonDblClick) {
            emit editSig(true);
        }
        if (ev->type() == QEvent::MouseButtonRelease) {
            const QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(ev);
            const Qt::MouseButtons button = mouseEvent->button();
            if (button == Qt::LeftButton) {

                return true;
            }
            if (button == Qt::RightButton) {
                QMenu contextMenu;
                QAction *action = new QAction(Tr::tr("Remove Board"));
                // const auto boardModel = qobject_cast<EasyBoardModel *>(model);
                contextMenu.addAction(action);
                connect(action, &QAction::triggered, this, [idx,this] {
                    const QVariant id = idx.data(EasyBoardModel::RoleId);
                    const ItemType itemType = idx.data(EasyBoardModel::RoleItemType).value<ItemType>();
                    // boardModel->removeFromList(id.toString(),itemType);
                    emit this->removeFromListSig(id.toString(),itemType);
                });
                contextMenu.addSeparator();
                action = new QAction(Tr::tr("Set As Default"));
                connect(action, &QAction::triggered, this, [idx,this] {
                    const QVariant id = idx.data(EasyBoardModel::RoleId);
                    const ItemType itemType = idx.data(EasyBoardModel::RoleItemType).value<ItemType>();
                    emit this->setDefaultSig(id.toString(),itemType);
                });
                contextMenu.addAction(action);
                contextMenu.exec(mouseEvent->globalPosition().toPoint());
                return true;
            }
        }
        return false;
    }
signals:
    void setDefaultSig(const QString &id,const ItemType &itemType);
    void removeFromListSig(const QString &id,const ItemType &itemType);
    void editSig(bool);
};

class SortFilterProxyModel : public QSortFilterProxyModel
{
public:
    struct SortOption {
        const QString displayName;
        const EasyBoardModel::Role role;
        const Qt::SortOrder order = Qt::AscendingOrder;
    };

    struct FilterOption {
        const QString displayName;
        const std::function<bool(const QModelIndex &)> indexAcceptedFunc;
    };

    SortFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

    static const QList<SortOption> &sortOptions()
    {
        static const QList<SortOption> options = {
            {Tr::tr("Name"), EasyBoardModel::RoleName},
            {Tr::tr("IP"), EasyBoardModel::RoleIp},
            {Tr::tr("Time"), EasyBoardModel::RoleDate, Qt::DescendingOrder},
        };
        return options;
    }

    void setSortOption(int index)
    {
        QTC_ASSERT(index < sortOptions().count(), index = 0);
        m_sortOptionIndex = index;
        const SortOption &option = sortOptions().at(index);

        // Ensure some order for cases with insufficient data, e.g. RoleDownloadCount
        setSortRole(EasyBoardModel::RoleName);
        sort(0);
        if (option.role == EasyBoardModel::RoleName)
            return; // Already sorted.

        setSortRole(option.role);
        sort(0, option.order);
    }

    static const QList<FilterOption> &filterOptions()
    {
        static const QList<FilterOption> options = {
            {
                Tr::tr("All"),
                []([[maybe_unused]] const QModelIndex &index) {
                    return true;
                },
            },
            {
                Tr::tr("Online"),
                [](const QModelIndex &index) {
                    return index.data(EasyBoardModel::RoleState).value<BoardState>() == Online;
                },
            },
            {
                Tr::tr("Loacl"),
                [](const QModelIndex &index) {
                    return index.data(EasyBoardModel::RoleItemType).value<ItemType>() == ItemTypeLocal;
                },
            },
            {
                Tr::tr("Network"),
                [](const QModelIndex &index) {
                    return index.data(EasyBoardModel::RoleItemType).value<ItemType>() == ItemTypeNetwork;
                },
            },
        };
        return options;
    }

    void setFilterOption(int index)
    {
        QTC_ASSERT(index < filterOptions().count(), index = 0);
        beginResetModel();
        m_filterOptionIndex = index;
        endResetModel();
    }

protected:
    // bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    // {
        // const SortOption &option = sortOptions().at(m_sortOptionIndex);
        // const ItemType leftType = left.data(RoleItemType).value<ItemType>();
        // const ItemType rightType = right.data(RoleItemType).value<ItemType>();
        // if (leftType != rightType)
        //     return option.order == Qt::AscendingOrder ? leftType < rightType
        //                                               : leftType > rightType;

    //     return QSortFilterProxyModel::lessThan(left, right);
    // }

    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override
    {
        const QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
        return filterOptions().at(m_filterOptionIndex).indexAcceptedFunc(index);
    }

    int m_filterOptionIndex = 0;
    int m_sortOptionIndex = 0;
};

class EasyBoardBrowserPrivate
{
public:
    bool dataFetched = false;
    EasyBoardModel *model;
    QLineEdit *searchBox;
    OptionChooser *filterChooser;
    OptionChooser *sortChooser;
    QListView *boardsView;
    QItemSelectionModel *selectionModel = nullptr;
    QSortFilterProxyModel *searchProxyModel;
    SortFilterProxyModel *sortFilterProxyModel;
    int columnsCount = 2;
    Tasking::TaskTreeRunner taskTreeRunner;
    SpinnerSolution::Spinner *m_spinner;
    QAbstractButton *addButton;
    QAbstractButton *updateButton;
    netproperty *pNetManage = nullptr;
};

EasyBoardBrowser::EasyBoardBrowser(QWidget *parent)
    : QWidget(parent)
    , d(new EasyBoardBrowserPrivate)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    d->pNetManage = new netproperty(this);

    static const TextFormat titleTF
        {Theme::Token_Text_Default, UiElementH2};
    QLabel *titleLabel = tfLabel(titleTF);
    titleLabel->setText(Tr::tr("Easy Board Configs"));

    d->searchBox = new SearchBox;
    d->searchBox->setPlaceholderText(Tr::tr("Search"));

    d->model = new EasyBoardModel(this);

    d->searchProxyModel = new QSortFilterProxyModel(this);
    d->searchProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    d->searchProxyModel->setFilterRole(EasyBoardModel::RoleSearchText);
    d->searchProxyModel->setSourceModel(d->model);

    d->sortFilterProxyModel = new SortFilterProxyModel(this);
    d->sortFilterProxyModel->setSourceModel(d->searchProxyModel);

    d->filterChooser = new OptionChooser(":/easyboard/images/filter.png",
                                         Tr::tr("Filter by: %1"));
    d->filterChooser->addItems(Utils::transform(SortFilterProxyModel::filterOptions(),
                                                &SortFilterProxyModel::FilterOption::displayName));

    d->sortChooser = new OptionChooser(":/easyboard/images/sort.png", Tr::tr("Sort by: %1"));
    d->sortChooser->addItems(Utils::transform(SortFilterProxyModel::sortOptions(),
                                              &SortFilterProxyModel::SortOption::displayName));


    d->addButton = new Button(Tr::tr("Add New"), Button::SmallPrimary);
    d->addButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    d->addButton->setToolTip(Tr::tr("Add remote board"));

    d->updateButton = new Button(Tr::tr("Auto Search"), Button::SmallPrimary);
    d->updateButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    d->updateButton->setToolTip(Tr::tr("Auto search local network board"));


    BoardItemDelegate *pDelegate = new BoardItemDelegate(this);
    connect(pDelegate,&BoardItemDelegate::setDefaultSig,d->model,&EasyBoardModel::setDefault);
    connect(pDelegate,&BoardItemDelegate::removeFromListSig,d->model,&EasyBoardModel::removeFromList);
    connect(pDelegate,&BoardItemDelegate::editSig,this, &EasyBoardBrowser::editBoardValue);

    d->boardsView = new QListView;
    d->boardsView->setFrameStyle(QFrame::NoFrame);
    d->boardsView->setItemDelegate(pDelegate);
    d->boardsView->setResizeMode(QListView::Adjust);
    d->boardsView->setSelectionMode(QListView::SingleSelection);
    d->boardsView->setUniformItemSizes(true);
    d->boardsView->setViewMode(QListView::IconMode);
    d->boardsView->setModel(d->sortFilterProxyModel);
    d->boardsView->setMouseTracking(true);

    d->model->setListView(d->boardsView);

    using namespace Layouting;
    Column {
        Column {
            titleLabel,
            customMargins(0, VPaddingM, 0, VPaddingM),
        },
        Row {
            d->searchBox,
            spacing(gapSize),
            customMargins(0, VPaddingM, extraListViewWidth() + gapSize, VPaddingM),
        },
        Row {
            d->addButton,
            spacing(gapSize*2),
            d->updateButton,
            spacing(gapSize),
            customMargins(0, VPaddingM, extraListViewWidth() + gapSize, VPaddingM),
        },
        Row {
            d->filterChooser,
            Space(HGapS),
            d->sortChooser,
            st,
            customMargins(0, 0, extraListViewWidth() + gapSize, 0),
        },

        d->boardsView,
        noMargin, spacing(0),
    }.attachTo(this);

    WelcomePageHelpers::setBackgroundColor(this, Theme::Token_Background_Default);
    WelcomePageHelpers::setBackgroundColor(d->boardsView, Theme::Token_Background_Default);
    WelcomePageHelpers::setBackgroundColor(d->boardsView->viewport(),
                                           Theme::Token_Background_Default);

    d->m_spinner = new SpinnerSolution::Spinner(SpinnerSolution::SpinnerSize::Large, this);
    d->m_spinner->hide();

    auto updateModel = [this] {
        d->sortFilterProxyModel->sort(0);
        // qDebug()<<"kong:"<<d->boardsView->currentIndex().isValid();
        emit itemChanged(d->boardsView->currentIndex(),d->boardsView->currentIndex());

        if (d->selectionModel == nullptr) {
            d->selectionModel = new QItemSelectionModel(d->sortFilterProxyModel,
                                                          d->boardsView);
            d->boardsView->setSelectionModel(d->selectionModel);
            connect(d->boardsView->selectionModel(), &QItemSelectionModel::currentChanged,
                    this, &EasyBoardBrowser::itemSelected);
        }
    };

    auto findEasyBoard = [this] {
        QModelIndex current = d->boardsView->currentIndex();
        this->d->pNetManage->findEasyBoard();
    };

    updateModel();

    auto editBoard = [this] {
        editBoardValue();
    };

    connect(d->addButton, &QAbstractButton::pressed,
            this, editBoard);
    connect(d->updateButton, &QAbstractButton::pressed,
            this, findEasyBoard);
    connect(d->pNetManage,&netproperty::getSocketData,
            d->model,&EasyBoardModel::onSocketData);
    connect(d->model, &EasyBoardModel::dataChange, this, updateModel);
    connect(d->searchBox, &QLineEdit::textChanged,
            d->searchProxyModel, &QSortFilterProxyModel::setFilterWildcard);
    connect(d->sortChooser, &OptionChooser::currentIndexChanged,
            d->sortFilterProxyModel, &SortFilterProxyModel::setSortOption);
    connect(d->filterChooser, &OptionChooser::currentIndexChanged,
            d->sortFilterProxyModel, &SortFilterProxyModel::setFilterOption);
}

EasyBoardBrowser::~EasyBoardBrowser()
{
    delete d;
}

void EasyBoardBrowser::editBoardValue(bool isEdit)
{
    QModelIndex current = d->boardsView->currentIndex();
    const QVariant id = current.data(EasyBoardModel::RoleId);
    const ItemType itemType = current.data(EasyBoardModel::RoleItemType).value<ItemType>();
    if(isEdit){
        if(current.isValid()){
            Board *pBoard = d->model->getIndexBoard(id.toString(),itemType);
            NewBoardDialog newDialog(ICore::dialogParent(),pBoard);
            // newDialog.setAutoLoadSession(d->isAutoRestoreLastSession());

            if (newDialog.exec() == QDialog::Accepted){
                d->model->updateIndex(pBoard->id);
            }
        }
        else{
            QMessageBox::question(nullptr, QApplication::translate("Application","ERROR"),
                                  QCoreApplication::translate("Application", "Current Select Item Error."),
                                  QMessageBox::Yes,
                                  QMessageBox::Yes);
        }
    }
    else{
        Board mBoard;
        mBoard.type = ItemTypeLocal;
        NewBoardDialog newDialog(ICore::dialogParent(),&mBoard);

        if (newDialog.exec() == QDialog::Accepted){
            d->model->addNewBoard(mBoard);
        }
    }
}
void EasyBoardBrowser::setFilter(const QString &filter)
{
    d->searchBox->setText(filter);
}

void EasyBoardBrowser::adjustToWidth(const int width)
{
    const int widthForItems = width - extraListViewWidth();
    d->columnsCount = qMax(1, qFloor(widthForItems / cellWidth));
    updateGeometry();
}

QSize EasyBoardBrowser::sizeHint() const
{
    const int columsWidth = d->columnsCount * cellWidth;
    return { columsWidth + extraListViewWidth(), 0};
}

int EasyBoardBrowser::extraListViewWidth() const
{
    // TODO: Investigate "transient" scrollbar, just for this list view.
    constexpr int extraPadding = qMax(0, ExVPaddingGapXl - gapSize);
    return d->boardsView->style()->pixelMetric(QStyle::PM_ScrollBarExtent)
           + extraPadding
           + 1; // Needed
}

void EasyBoardBrowser::showEvent(QShowEvent *event)
{
    if (!d->dataFetched) {
        d->dataFetched = true;
        fetchExtensions();
    }
    QWidget::showEvent(event);
}

static QString customOsTypeToString(OsType osType)
{
    switch (osType) {
    case OsTypeWindows:
        return "Windows";
    case OsTypeLinux:
        return "Linux";
    case OsTypeMac:
        return "macOS";
    case OsTypeOtherUnix:
        return "Other Unix";
    case OsTypeOther:
    default:
        return "Other";
    }
}

void EasyBoardBrowser::fetchExtensions()
{
#ifdef WITH_TESTS
    // Uncomment for testing with local json data.
    // Available: "augmentedplugindata", "defaultpacks", "varieddata", "thirdpartyplugins"
    // d->model->setExtensionsJson(testData("defaultpacks")); return;
#endif // WITH_TESTS
    // d->model->setBoards({});
    // if (!settings().useExternalRepo()) {
    //     d->model->setBoards({});
    //     return;
    // }

    // using namespace Tasking;

    // const auto onQuerySetup = [this](NetworkQuery &query) {
    //     const QString url = "%1/api/v1/search?request=";
    //     const QString requestTemplate
    //         = R"({"qtc_version":"%1","host_os":"%2","host_os_version":"%3","host_architecture":"%4","page_size":200})";
    //     const QString request = url.arg(settings().externalRepoUrl()) + requestTemplate
    //                                                                         .arg(QCoreApplication::applicationVersion())
    //                                                                         .arg(customOsTypeToString(HostOsInfo::hostOs()))
    //                                                                         .arg(QSysInfo::productVersion())
    //                                                                         .arg(QSysInfo::currentCpuArchitecture());
    //     query.setRequest(QNetworkRequest(QUrl::fromUserInput(request)));
    //     query.setNetworkAccessManager(NetworkAccessManager::instance());
    //     qCDebug(browserLog).noquote() << "Sending JSON request:" << request;
    //     d->m_spinner->show();
    // };

    // qDebug()<<"kong:"<<settings().externalRepoUrl();
    // d->model->setBoards({});


    // const auto onQueryDone = [this](const NetworkQuery &query, DoneWith result) {
    //     const QByteArray response = query.reply()->readAll();
    //     qCDebug(browserLog).noquote() << "Got JSON QNetworkReply:" << query.reply()->error();
    //     if (result == DoneWith::Success) {
    //         qCDebug(browserLog).noquote() << "JSON response size:"
    //                                       << QLocale::system().formattedDataSize(response.size());
    //         d->model->setBoards(response);
    //     } else {
    //         qCWarning(browserLog).noquote() << response;
    //         d->model->setBoards({});
    //     }
    //     d->m_spinner->hide();
    // };

    // Group group {
    //             NetworkQueryTask{onQuerySetup, onQueryDone},
    //             };

    // d->taskTreeRunner.start(group);
}

QLabel *tfLabel(const TextFormat &tf, bool singleLine)
{
    QLabel *label = singleLine ? new Utils::ElidingLabel : new QLabel;
    if (singleLine)
        label->setFixedHeight(tf.lineHeight());
    label->setFont(tf.font());
    label->setAlignment(Qt::Alignment(tf.drawTextFlags));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, tf.color());
    label->setPalette(pal);

    return label;
}

QPixmap itemIcon(const QModelIndex &index, Size size)
{
    const QSize iconBgS = size == SizeSmall ? iconBgSizeSmall : iconBgSizeBig;
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pixmap(iconBgS * dpr);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);
    const QRect iconBgR(QPoint(), pixmap.deviceIndependentSize().toSize());

    // const PluginSpec *ps = pluginSpecForName(index.data(RoleName).toString());
    const bool isEnabled = true;//= ps == nullptr || ps->isEffectivelyEnabled();
    const QGradientStops gradientStops = {
                                          {0, creatorColor(isEnabled ? Theme::Token_Gradient01_Start
                                                                     : Theme::Token_Gradient02_Start)},
                                          {1, creatorColor(isEnabled ? Theme::Token_Gradient01_End
                                                                     : Theme::Token_Gradient02_End)},
                                          };

    const Theme::Color color = Theme::Token_Basic_White;

    static const QIcon board = Icon({{":/easyboard/images/common-board.png",
                                           color}}, Icon::Tint).icon();
    // const ItemType itemType = index.data(RoleItemType).value<ItemType>();
    const QIcon &icon = (size == SizeSmall ? board : board);

    const int iconRectRounding = 4;
    const qreal iconOpacityDisabled = 0.6;

    QPainter p(&pixmap);
    QLinearGradient gradient(iconBgR.topRight(), iconBgR.bottomLeft());
    gradient.setStops(gradientStops);
    WelcomePageHelpers::drawCardBackground(&p, iconBgR, gradient, Qt::NoPen, iconRectRounding);
    if (!isEnabled)
        p.setOpacity(iconOpacityDisabled);
    icon.paint(&p, iconBgR);

    return pixmap;
}

QPixmap boardIcon(const QModelIndex &index, Size size)
{
    const QSize iconBgS = size == SizeImge ? imgBgSize : iconBgSizeBig;
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pixmap(iconBgS * dpr);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);
    const QRect iconBgR(QPoint(), pixmap.deviceIndependentSize().toSize());

    const bool isEnabled = true;
    const QGradientStops gradientStops = {
                                          {0, creatorColor(isEnabled ? Theme::Token_Gradient01_Start
                                                                     : Theme::Token_Gradient02_Start)},
                                          {1, creatorColor(isEnabled ? Theme::Token_Gradient01_End
                                                                     : Theme::Token_Gradient02_End)},
                                          };

    const Theme::Color color = Theme::Token_Background_Default;

    static const QIcon board = Icon(":/easyboard/images/t113-s3.png").icon();
    // const ItemType itemType = index.data(RoleItemType).value<ItemType>();
    const QIcon &icon = (size == SizeSmall ? board : board);

    const int iconRectRounding = 4;
    const qreal iconOpacityDisabled = 0.6;

    QPainter p(&pixmap);
    QLinearGradient gradient(iconBgR.topRight(), iconBgR.bottomLeft());
    gradient.setStops(gradientStops);
    // WelcomePageHelpers::drawCardBackground(&p, iconBgR, gradient, Qt::NoPen, iconRectRounding);
    // if (!isEnabled)
    //     p.setOpacity(iconOpacityDisabled);
    icon.paint(&p, iconBgR);

    return pixmap;
}

#include "easyboardbrowser.moc"
} // ExtensionManager::Internal
