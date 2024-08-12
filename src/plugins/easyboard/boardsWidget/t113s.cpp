#include "t113s.h"
#include "easyboardtr.h"
#include "easyboardbrowser.h"
#include "easyboardmodel.h"

#include <coreplugin/welcomepagehelper.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/iwelcomepage.h>


#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>
#include <extensionsystem/pluginview.h>

#include <solutions/tasking/networkquery.h>
#include <solutions/tasking/tasktree.h>
#include <solutions/tasking/tasktreerunner.h>

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/icon.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/networkaccessmanager.h>
#include <utils/stylehelper.h>
#include <utils/temporarydirectory.h>
#include <utils/utilsicons.h>

// using namespace Core;
// using namespace Utils;
// using namespace StyleHelper;
// using namespace WelcomePageHelpers;


namespace EasyBoard::Internal {

Q_LOGGING_CATEGORY(t113sLog, "qtc.easyboard.widget", QtWarningMsg)

t113s::t113s(QWidget *parent):QWidget(parent)
{
    // m_img = new QImage(":/easyboard/images/t113-s3.png");
    m_icon = new QLabel;
    m_icon->setFixedSize(imgBgSize);

    static const TextFormat titleTF
        {Theme::Token_Text_Default, UiElementH4};
    static const TextFormat vendorTF
        {Theme::Token_Text_Accent, UiElementLabelMedium};
    static const TextFormat dlTF
        {Theme::Token_Text_Muted, vendorTF.uiElement};
    static const TextFormat detailsTF
        {Theme::Token_Text_Default, UiElementBody2};

    m_title = tfLabel(titleTF);
    m_vendor = new Button({}, Button::SmallLink);
    m_vendor->setContentsMargins({});
    m_divider = new QLabel;
    m_divider->setFixedSize(1, dividerH);
    WelcomePageHelpers::setBackgroundColor(m_divider, dlTF.themeColor);

    m_details = tfLabel(detailsTF);
    installButton = new Button(Tr::tr("Default"), Button::MediumPrimary);
    installButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    // installButton->hide();

    using namespace Layouting;
    Row {
        m_icon,
        Column {
            m_title,
            st,
            Row {
                m_vendor,
                Widget {
                    // bindTo(&m_dlCountItems),
                    Row {
                        Space(SpacingTokens::HGapXs),
                        m_divider,
                        Space(SpacingTokens::HGapXs),
                        // m_dlIcon,
                        Space(SpacingTokens::HGapXxs),
                        // m_dlCount,
                        noMargin, spacing(0),
                    },
                },
            },
            st,
            m_details,
            spacing(0),
        },
        noMargin, spacing(SpacingTokens::ExPaddingGapL),
    }.attachTo(this);

    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Maximum);
    // m_dlCountItems->setVisible(false);

    // connect(installButton, &QAbstractButton::pressed,
    //         this, &HeadingWidget::pluginInstallationRequested);
    // connect(m_vendor, &QAbstractButton::pressed, this, [this]() {
    //     emit vendorClicked(m_currentVendor);
    // });

    update({});
}

void t113s::update(const QModelIndex &current)
{
    if (!current.isValid())
        return;

    m_icon->setPixmap(boardIcon(current, SizeBig));

    const QString name = current.data(EasyBoardModel::RoleName).toString();
    m_title->setText(name);

    m_currentVendor = current.data(EasyBoardModel::RoleDate).toString();
    m_vendor->setText(m_currentVendor);

    m_details->setText(current.data(EasyBoardModel::RoleDescriptionText).toString());

    const ItemType itemType = current.data(EasyBoardModel::RoleItemType).value<ItemType>();
    const bool isPack = itemType == ItemTypeLocal;
    const bool isRemotePlugin = false;//!(isPack || pluginSpecForName(name));
    installButton->setVisible(true);
    if (installButton->isVisible())
        installButton->setToolTip("Set As Default");
}

}
