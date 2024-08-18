#include "t113s.h"
#include "easyboardtr.h"
#include "easyboardbrowser.h"
#include "easyboardmodel.h"

#include <coreplugin/welcomepagehelper.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/iwelcomepage.h>
#include <coreplugin/minisplitter.h>


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


using namespace Core;
using namespace Utils;
using namespace StyleHelper;
using namespace WelcomePageHelpers;


namespace EasyBoard::Internal {

Q_LOGGING_CATEGORY(t113sLog, "qtc.easyboard.widget", QtWarningMsg)

constexpr TextFormat contentTF
    {Theme::Token_Text_Default, UiElement::UiElementBody2};

t113s::t113s(QWidget *parent):QWidget(parent)
{

    m_description = tfLabel(contentTF, false);
    m_right = tfLabel(contentTF, false);

    m_description->setText("value test ..................");

    auto leftWidget = new QWidget;
    auto rightWidget = new QWidget;

    using namespace Layouting;

    Column {
        m_description,
        st,
        noMargin, spacing(SpacingTokens::ExVPaddingGapXl),
    }.attachTo(leftWidget);

    Column {
        m_right,
        st,
        noMargin, spacing(SpacingTokens::ExVPaddingGapXl),
    }.attachTo(rightWidget);


    auto splitter = new MiniSplitter(Qt::Horizontal);
    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);

    Column{
        splitter,
    }.attachTo(this);

    update({});
}

void t113s::update(const QModelIndex &current)
{
    if (!current.isValid())
        return;
    m_description->setText("xxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    m_right->setText("ksdflskdfslkdf");
}

}
