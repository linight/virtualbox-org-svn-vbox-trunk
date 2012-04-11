/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * UIWizardPage class declaration
 */

/*
 * Copyright (C) 2009-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __UIWizardPage_h__
#define __UIWizardPage_h__

/* Global includes: */
#include <QWizardPage>

/* Local includes: */
#include "QIWithRetranslateUI.h"

/* Forward declarations: */
class UIWizard;

/* QWizardPage class reimplementation with extended funtionality. */
class UIWizardPage : public QIWithRetranslateUI<QWizardPage>
{
    Q_OBJECT;

public:

    /* Constructor: */
    UIWizardPage();

    /* Translation stuff: */
    void retranslate() { retranslateUi(); }

protected:

    /* Helpers: */
    UIWizard* wizard() const;
    QString standardHelpText() const;
    void startProcessing();
    void endProcessing();
};

#endif // __UIWizardPage_h__

