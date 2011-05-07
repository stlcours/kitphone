// kitphone.h --- 
// 
// Author: liuguangzhao
// Copyright (C) 2007-2010 liuguangzhao@users.sf.net
// URL: 
// Created: 2010-11-20 17:46:16 +0800
// Version: $Id: kitphone.h 869 2011-05-07 09:41:17Z drswinghead $
// 
#ifndef _KITPHONE_H_
#define _KITPHONE_H_

#include <QtCore>
#include <QtNetwork>
#include <QMainWindow>

class SkypePhone;
class SipPhone;

namespace Ui {
    class KitPhone;
}

class KitPhone : public QMainWindow
{
    Q_OBJECT;
public:
    explicit KitPhone(QWidget *parent = 0);
    virtual ~KitPhone();

protected:
    virtual void 	paintEvent ( QPaintEvent * event );
    virtual void 	showEvent ( QShowEvent * event );
                                                  
private:
    Ui::KitPhone *uiw;

    SkypePhone *uiw_skype;
    SipPhone *uiw_sip;
};

#endif /* _KITPHONE_H_ */
