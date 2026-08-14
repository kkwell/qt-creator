// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "ieasyboardpage.h"

namespace Core {

static QList<IEasyBoardPage *> g_easyboardPages;

const QList<IEasyBoardPage *> IEasyBoardPage::allEasyBoardPages()
{
    return g_easyboardPages;
}

IEasyBoardPage::IEasyBoardPage()
{
    g_easyboardPages.append(this);
}

IEasyBoardPage::~IEasyBoardPage()
{
    g_easyboardPages.removeOne(this);
}

} // namespace Core
