#ifndef _SKYPECOMM_H
#define _SKYPECOMM_H

/***************************************************************
 * skypeComm.h
 * @Author:      Jonathan Verner (jonathan.verner@matfyz.cz)
 * @License:     GPL v2.0 or later
 * @Created:     2008-05-15.
 * @Last Change: 2008-05-15.
 * @Revision:    $Id: skypecommon.h 175 2010-10-16 09:02:39Z drswinghead $
 * Description:
 * Usage:
 * TODO:
 * CHANGES:
 ***************************************************************/

#include <QtCore/QString>
#include <QtCore/QThread>
#include <QtGui/QWidget>

#ifdef Q_WS_X11
#include "xmessages.h"
#include <X11/Xlib.h>
#endif

#ifdef Q_WS_WIN
#include <QtCore/QEventLoop>
#include <windows.h>

class EventLoopThread : public QThread
{
    Q_OBJECT;
public:
    explicit EventLoopThread(QObject *parent = 0);
    virtual ~EventLoopThread();

    void run();

    void fireMessage(const QString &message);

signals:
    void newMessageFromSkype(const QString &message);

};

#endif

class SkypeCommon : public QObject {
    Q_OBJECT;
    //private:
public:
    WId skype_win;

#ifdef Q_WS_X11
    XMessages *msg;
#endif

#ifdef Q_WS_WIN
    static QWidget *mainWin;
    static WId main_window;
    bool connected, refused, tryLater;
    static UINT attachMSG, discoverMSG;

    QEventLoop localEventLoop;
    long TimeOut;
    bool waitingForConnect;

private slots:
    void timeOut();
#endif

public:
    explicit SkypeCommon();
    virtual ~SkypeCommon();

    bool sendMsgToSkype(const QString &message);
    bool attachToSkype();
    bool detachSkype();
    bool is_skype_running();

private slots:
#ifdef Q_OS_WIN
    // void onComCommand(IDispatch *pcmd);
    // void onComSignal(const QString & name, int argc, void * argv);
    // void onComException(int code, const QString & source, const QString & desc, const QString & help);
#endif

signals:
    void newMsgFromSkype(const QString &message);
    void skypeNotFound();
	
protected slots:

#ifdef Q_WS_X11
    void processX11Message(int win, const QString &message);
#endif

#ifdef Q_WS_WIN
    void processWINMessage( MSG *msg );
#endif


};


#endif /* _SKYPECOMM_H */
