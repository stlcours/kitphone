// kitpstn.cpp --- 
// 
// Author: liuguangzhao
// Copyright (C) 2007-2010 liuguangzhao@users.sf.net
// URL: 
// Created: 2010-10-20 17:20:22 +0800
// Version: $Id: skypephone.cpp 998 2011-09-17 11:03:58Z drswinghead $
// 

#include <QtCore>
#include <QtGui>

#include "simplelog.h"
#include "networkdetect.h"

#include "ui_skypephone.h"
#include "skypephone.h"

#include "metauri.h"
#include "skycit.h"
#include "skypetunnel.h"
#include "skypetracer.h"

#include "websocketclient.h"
#include "websocketclient2.h"
#include "asyncdatabase.h"
#include "phonecontact.h"
#include "phonecontactproperty.h"
#include "groupinfodialog.h"
#include "contactmodel.h"
#include "callhistorymodel.h"

/*
  由于在开始设计的时候，以页面版本为主，而在页面上能得到skype信息非常少，
  不得不使用websocket传递客户端到服务器端的控制信息，像呼出，中间信息，挂断等。
  在实现这简单的桌面客户端后，上面这种老的方式显示有点笨了，
  最好的实现方式，使用websocket只用作传输一些提示信息，不作为控制，
  由于客户端方式能得到所有skype状态信息，通过skype的状态变化控制更有效，稳定。
  现阶段，还不能完全使用新的方式，还需要以页面拨打方式为主。
 */
SkypePhone::SkypePhone(QWidget *parent)
    :QWidget(parent)
    , uiw(new Ui::SkypePhone())
{
    this->uiw->setupUi(this);

    this->m_dialpanel_layout_index = 2;
    this->m_dialpanel_layout_item = this->layout()->itemAt(this->m_dialpanel_layout_index);
    int call_state_layout_index = 3;
    QLayoutItem *call_state_layout_item = this->layout()->itemAt(call_state_layout_index);
    this->m_call_state_widget = call_state_layout_item->widget();
    int log_list_layout_index = 5;
    QLayoutItem *log_list_layout_item = this->layout()->itemAt(log_list_layout_index);
    this->m_log_list_widget = log_list_layout_item->widget();

    this->m_status_bar = nullptr;

    this->m_call_button_disable_count = 1;

    this->defaultPstnInit();
    // this->m_adb = new AsyncDatabase();
    this->m_adb = boost::shared_ptr<AsyncDatabase>(new AsyncDatabase());
    QObject::connect(this->m_adb.get(), SIGNAL(connected()), this, SLOT(onDatabaseConnected()));
    this->m_adb->start();
    // QTimer::singleShot(50, this->m_adb.get(), SLOT(start()));
    
    QObject::connect(this->m_adb.get(), SIGNAL(results(const QList<QSqlRecord>&, int, bool, const QString&, const QVariant&)),
                     this, SLOT(onSqlExecuteDone(const QList<QSqlRecord>&, int, bool, const QString&, const QVariant&)));


    this->m_contact_model = new ContactModel(this->m_adb);
    this->m_call_history_model = new CallHistoryModel(this->m_adb);

    //////
    this->m_call_button_disable_count.ref();
    QHostInfo::lookupHost("gw.skype.tom.com", this, SLOT(onCalcWSServByNetworkType(QHostInfo)));
    this->log_output(LT_USER, "检测网络类型...");
}

SkypePhone::~SkypePhone()
{
    this->uiw->treeView->setModel(0);
    delete this->m_contact_model;

    this->m_adb->quit();
    this->m_adb->wait();
    // delete this->m_adb;
    this->m_adb = boost::shared_ptr<AsyncDatabase>();

    delete uiw;
}

void SkypePhone::paintEvent ( QPaintEvent * event )
{
    QWidget::paintEvent(event);
    // qDebug()<<"parintttttt"<<event<<event->type();
    if (this->first_paint_event) {
        this->first_paint_event = false;
		// QTimer::singleShot(50, this, SLOT(main_ui_draw_complete()));
        // this->m_adb->start();        
        // QTimer::singleShot(30, this->m_adb.get(), SLOT(start()));
    }
}
void SkypePhone::showEvent ( QShowEvent * event )
{
    QWidget::showEvent(event);
    // qDebug()<<"showwwwwwwwwwwww"<<event<<event->type();
}

// 实现label的click事件
bool SkypePhone::eventFilter(QObject *obj, QEvent *evt)
{
     if (obj == this->uiw->label_11) {
         // QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
         // qDebug("Ate key press %d", keyEvent->key());
         if (evt->type() == QEvent::MouseButtonRelease) {
             QMouseEvent *mevt = static_cast<QMouseEvent*>(evt);
             this->uiw->toolButton->click();
             return true;
         }
     }

     // standard event processing
     return QObject::eventFilter(obj, evt);
}

void SkypePhone::init_status_bar(QStatusBar *bar)
{
    this->m_status_bar = bar;
}

// call only once
void SkypePhone::defaultPstnInit()
{
    this->mSkype = NULL;
    this->mtun = NULL;
    this->mSkypeTracer = NULL;
    this->m_curr_skype_call_id = -1;

    // QObject::connect(this->uiw->checkBox_2, SIGNAL(stateChanged(int)),
    //                  this, SLOT(onInitPstnClient()));

    this->mdyn_oe = new QGraphicsOpacityEffect();
    QObject::connect(this->uiw->toolButton_2, SIGNAL(clicked()),
                     this, SLOT(onShowDialPanel()));
    QObject::connect(this->uiw->toolButton, SIGNAL(clicked()),
                     this, SLOT(onShowLogPanel()));
    QObject::connect(this->uiw->pushButton_2, SIGNAL(clicked()),
                     this, SLOT(onShowLogPanel()));

    QObject::connect(this->uiw->pushButton, SIGNAL(clicked()),
                     this, SLOT(onShowSkypeTracer()));
    QObject::connect(this->uiw->pushButton_3, SIGNAL(clicked()),
                     this, SLOT(onConnectSkype()));
    // QObject::connect(this->mainUI.pushButton_2, SIGNAL(clicked()),
    //                  this, SLOT(onSendPackage()));
    QObject::connect(this->uiw->pushButton_4, SIGNAL(clicked()),
                     this, SLOT(onCallPstn()));
    QObject::connect(this->uiw->toolButton_10, SIGNAL(clicked()),
                     this, SLOT(onHangupPstn()));

    // QObject::connect(this->uiw->toolButton_6, SIGNAL(clicked()),
    //                  this, SLOT(onAddContact()));

    this->customAddContactButtonMenu();
    this->initContactViewContextMenu();
    this->initHistoryViewContextMenu();

    // this->m_dialpanel_layout_item->widget()->setVisible(false);
    // this->m_call_state_layout_item->widget()->setVisible(false);
    this->m_call_state_widget->setVisible(false);

    this->uiw->label_11->installEventFilter(this);

    QObject::connect(this->uiw->toolButton_14, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));
    QObject::connect(this->uiw->toolButton_15, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));
    QObject::connect(this->uiw->toolButton_16, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));
    QObject::connect(this->uiw->toolButton_17, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));
    QObject::connect(this->uiw->toolButton_18, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));
    QObject::connect(this->uiw->toolButton_19, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));
    QObject::connect(this->uiw->toolButton_20, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));
    QObject::connect(this->uiw->toolButton_21, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));
    QObject::connect(this->uiw->toolButton_22, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));
    QObject::connect(this->uiw->toolButton_23, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));
    QObject::connect(this->uiw->toolButton_24, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));
    QObject::connect(this->uiw->toolButton_25, SIGNAL(clicked()), this, SLOT(onDigitButtonClicked()));

}

void SkypePhone::customAddContactButtonMenu()
{
    QAction *action;
    QAction *daction;
    QMenu *add_contact_menu = new QMenu(this);

    daction = new QAction(QIcon(":/skins/default/addcontact.png"), tr("Add Contact"), this);
    add_contact_menu->addAction(daction);
    // add_contact_menu->setDefaultAction(action);

    QObject::connect(daction, SIGNAL(triggered()),
                     this, SLOT(onAddContact()));


    action = new QAction(tr("Import Contacts"), this);
    add_contact_menu->addAction(action);

    action = new QAction(tr("Export Contacts"), this);
    add_contact_menu->addAction(action);

    add_contact_menu->addSeparator();

    action = new QAction(tr("New Group"), this);
    add_contact_menu->addAction(action);
    QObject::connect(action, SIGNAL(triggered()), this, SLOT(onAddGroup()));

    this->uiw->toolButton_3->setMenu(add_contact_menu);
    this->uiw->toolButton_3->setDefaultAction(daction);
}

void SkypePhone::initContactViewContextMenu()
{
    QAction *action;

    this->m_contact_view_ctx_menu = new QMenu(this);

    action = new QAction(tr("&Call..."), this);
    this->m_contact_view_ctx_menu->addAction(action);
    QObject::connect(action, SIGNAL(triggered()), this, SLOT(onPlaceCallFromContactList()));
    
    this->m_contact_view_ctx_menu->addSeparator();

    action = new QAction(tr("&Edit Contact"), this);
    this->m_contact_view_ctx_menu->addAction(action);
    QObject::connect(action, SIGNAL(triggered()), this, SLOT(onModifyContact()));

    action = new QAction(tr("&Delete Contact"), this);
    this->m_contact_view_ctx_menu->addAction(action);

    this->m_contact_view_ctx_menu->addSeparator();

    action = new QAction(tr("&Delete Group"), this);
    this->m_contact_view_ctx_menu->addAction(action);

    QObject::connect(this->uiw->treeView, SIGNAL(customContextMenuRequested(const QPoint &)),
                     this, SLOT(onShowContactViewMenu(const QPoint &)));
}

void SkypePhone::initHistoryViewContextMenu()
{
    QAction *action;

    this->m_history_view_ctx_menu = new QMenu(this);
    
    action = new QAction(tr("&Call..."), this);
    this->m_history_view_ctx_menu->addAction(action);
    QObject::connect(action, SIGNAL(triggered()), this, SLOT(onPlaceCallFromHistory()));

    this->m_history_view_ctx_menu->addSeparator();

    action = new QAction(tr("&Delete"), this);
    this->m_history_view_ctx_menu->addAction(action);
    QObject::connect(action, SIGNAL(triggered()), this, SLOT(onDeleteCallHistory()));

    action = new QAction(tr("Delete &All Calls"), this);
    this->m_history_view_ctx_menu->addAction(action);
    QObject::connect(action, SIGNAL(triggered()), this, SLOT(onDeleteAllCallHistory()));

    QObject::connect(this->uiw->treeView_2, SIGNAL(customContextMenuRequested(const QPoint &)),
                     this, SLOT(onShowHistoryViewMenu(const QPoint &)));

}

// maybe called twice or more
void SkypePhone::onInitPstnClient()
{
    if (this->mtun != NULL) {
        delete this->mtun;
        this->mtun = NULL;
    }
    if (this->mSkype != NULL) {
        this->mSkype->disconnect();
        delete this->mSkype;
        this->mSkype = NULL;
    }
    this->mSkype = new Skycit("karia2");
    this->mSkype->setRunAsClient();
    // this->mSkype->connectToSkype();
    QObject::connect(this->mSkype, SIGNAL(skypeError(int, QString, QString)),
                     this, SLOT(onSkypeError(int, QString, QString)));
    QObject::connect(this->mSkype, SIGNAL(skypeNotFound()),
                     this, SLOT(onSkypeNotFound()));
    
    QObject::connect(this->mSkype, SIGNAL(connected(QString)), this, SLOT(onSkypeConnected(QString)));
    QObject::connect(this->mSkype, SIGNAL(realConnected(QString)),
                     this, SLOT(onSkypeRealConnected(QString)));

    QObject::connect(this->mSkype, SIGNAL(UserStatus(QString,int)),
                     this, SLOT(onSkypeUserStatus(QString,int)));

    QObject::connect(this->mSkype, SIGNAL(newCallArrived(QString,QString,int)),
                     this, SLOT(onSkypeCallArrived(QString,QString,int)));
    QObject::connect(this->mSkype, SIGNAL(callHangup(QString,QString,int)),
                     this, SLOT(onSkypeCallHangup(QString,QString,int)));

    this->mtun = new SkypeTunnel(this);
    this->mtun->setSkype(this->mSkype);
}

void SkypePhone::onShowSkypeTracer()
{
    if (this->mSkype == nullptr) {
        this->log_output(LT_USER, "未连接到Skype API。");
        return;
    }
    if (this->mSkypeTracer == NULL) {
        this->mSkypeTracer = new SkypeTracer(this);
        QObject::connect(this->mSkype, SIGNAL(commandRequest(QString)),
                         this->mSkypeTracer, SLOT(onCommandRequest(QString)));
        QObject::connect(this->mSkype, SIGNAL(commandResponse(QString, QString)),
                         this->mSkypeTracer, SLOT(onCommandResponse(QString, QString)));
        QObject::connect(this->mSkypeTracer, SIGNAL(commandRequest(QString)),
                         this->mSkype, SLOT(onCommandRequest(QString)));
    }

    this->mSkypeTracer->setVisible(!this->mSkypeTracer->isVisible());
}

void SkypePhone::onConnectSkype()
{
    this->log_output(LT_USER, "正在连接Skype API。。。，");
    this->log_output(LT_USER, "请注意Skype客户端弹出的认证提示。");
    this->uiw->pushButton_3->setEnabled(false);
    this->onInitPstnClient();

    qDebug()<<"Skype name:"<<this->mSkype->handlerName();
}

void SkypePhone::onConnectApp2App()
{
    // QString skypeName = this->uiw->lineEdit->text();
    // this->mtun->setStreamPeer(skypeName);

    // QStringList contacts = this->mSkype->getContacts();
    // qDebug()<<skypeName<<contacts;

    // this->mSkype->newStream(skypeName);
    // this->mSkype->newStream("drswinghead");
}

void SkypePhone::onSkypeNotFound()
{
    qLogx()<<"";
    this->m_call_button_disable_count.ref();
    this->uiw->pushButton_3->setEnabled(true);
    this->log_output(LT_USER, QString("未安装或未登陆Skype客户端"));
}

void SkypePhone::onSkypeConnected(QString user_name)
{
    qDebug()<<"Waiting handler name:"<<user_name;
}

void SkypePhone::onSkypeRealConnected(QString user_name)
{
    qDebug()<<"Got handler name:"<<user_name;
    this->uiw->label_3->setText(user_name);

    // count == 0
    if (this->m_call_button_disable_count.deref() == false) {
        this->uiw->pushButton_4->setEnabled(true);
    }

    this->log_output(LT_USER, "连接Skype API成功，用户名：" + user_name);
}

void SkypePhone::onSkypeUserStatus(QString str_status, int int_status)
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<str_status<<int_status;
    QPixmap icon = QPixmap(":/skins/default/status_offline.png");
    switch (int_status) {
    case SS_ONLINE:
    case SS_INVISIBLE:
    case SS_SKYPEME:
        icon = QPixmap(":/skins/default/status_online.png");
        break;
    case SS_AWAY:
        icon = QPixmap(":/skins/default/status_away.png");
        break;
    case SS_DND:
        icon = QPixmap(":/skins/default/status_dnd.png");
        break;
    case SS_NA:
        icon = QPixmap(":/skins/default/status_busy.png");
        break;
    case SS_OFFLINE:
        break;
    default:
        break;
    }

    this->uiw->label_14->setPixmap(icon);
    
    QString old_tooltip = this->uiw->label_14->toolTip();
    QString new_tooltip = old_tooltip.split(":").at(0) + ": " + str_status;
    this->uiw->label_14->setToolTip(new_tooltip);
}

void SkypePhone::onSkypeError(int code, QString msg, QString cmd)
{
    qLogx()<<code<<msg<<cmd;
}

void SkypePhone::onSkypeCallArrived(QString callerName, QString calleeName, int callID)
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<callerName<<calleeName<<callID;
    this->m_curr_skype_call_id = callID;
    this->m_curr_skype_call_peer = calleeName;
}

void SkypePhone::onSkypeCallHangup(QString contactName, QString callerName, int callID)
{
    qLogx()<<"";
    int skype_call_id = callID;

    if (callID != this->m_curr_skype_call_id) {
        qLogx()<<"Warning:";
    }
    this->m_curr_skype_call_id = -1;
    if (this->m_call_state_widget->isVisible()) {
        this->onDynamicSetVisible(this->m_call_state_widget, false);
    }

    if (this->wscli.get() && this->wscli->isClosed()) {
        qLogx()<<"ws ctrl unexception closed, should cleanup here now.";
        this->log_output(LT_USER, "通话异常终止，请重试。");

        {
            this->onDynamicSetVisible(this->m_call_state_widget, false);
            // 关闭网络连续，如果有的话。
            this->wscli.reset();
            this->uiw->pushButton_4->setEnabled(true);
        }
    }
}

void SkypePhone::onDigitButtonClicked()
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<(sender());
    QToolButton *btn = static_cast<QToolButton*>(sender());

    QString digit;
    QHash<QToolButton*, char> bdmap;
    bdmap[this->uiw->toolButton_14] = '0';
    bdmap[this->uiw->toolButton_15] = '1';
    bdmap[this->uiw->toolButton_16] = '2';
    bdmap[this->uiw->toolButton_17] = '3';
    bdmap[this->uiw->toolButton_18] = '4';
    bdmap[this->uiw->toolButton_19] = '5';
    bdmap[this->uiw->toolButton_20] = '6';
    bdmap[this->uiw->toolButton_21] = '7';
    bdmap[this->uiw->toolButton_22] = '8';
    bdmap[this->uiw->toolButton_23] = '9';
    bdmap[this->uiw->toolButton_24] = '*';
    bdmap[this->uiw->toolButton_25] = '#';

    Q_ASSERT(bdmap.contains(btn));

    // 作为电话号码的一部分
    if (this->m_curr_skype_call_id == -1) {
        QString num = this->uiw->comboBox_3->currentText();
        QLineEdit *le = this->uiw->comboBox_3->lineEdit();
        if (le->selectedText() == num) {
            le->setText(QString(QChar(bdmap[btn])));
        } else if (le->selectedText().length() > 0) {
            le->insert(QString(QChar(bdmap[btn])));
        } else {
            le->insert(QString(QChar(bdmap[btn])));
        }
    }
    else
    // if (in call state) it's dtmf digit
    if (this->m_curr_skype_call_id != -1) {
        // send dtmf here
        this->mSkype->setCallDTMF(QString::number(this->m_curr_skype_call_id), 
                                  QString(QChar(bdmap[btn])));
    }
    else { 
        Q_ASSERT(1==2);
    }
}


void SkypePhone::onCallPstn()
{
    qLogx()<<sender()<<this->m_conn_ws_retry_times;
    if (sender()->isWidgetType()) { // from button
        // init call by user
        this->m_conn_ws_retry_times = this->m_conn_ws_max_retry_times;

        this->uiw->pushButton_4->setEnabled(false);
        // 显示呼叫状态窗口
        this->onDynamicSetVisible(this->m_call_state_widget, true);
    } else {
        // retry call by robot
        if (--this->m_conn_ws_retry_times >= 0) {
            qLogx()<<"retry conn ws: "<< (this->m_conn_ws_max_retry_times-this->m_conn_ws_retry_times);
        } else {
            qLogx()<<"retry conn ws exceed max retry:"<<this->m_conn_ws_max_retry_times;
            this->onDynamicSetVisible(this->m_call_state_widget, false);
            // 关闭网络连续，如果有的话。
            this->wscli.reset();

            this->log_output(LT_USER, "网络错误，呼叫已中止。");
            this->uiw->pushButton_4->setEnabled(true);
            return;
        }
    }
    // 检测号码有效性
    
    // 设置呼叫状态
    QString phone_number = this->uiw->comboBox_3->currentText();
    // this->uiw->pushButton_4->setEnabled(false);
    this->uiw->label_5->setText(phone_number);
    
    Q_ASSERT(!this->m_ws_serv_ipaddr.isEmpty());
    // QString wsuri = "ws://202.108.12.212:80/" + this->mSkype->handlerName() + "/";
    // /caller_name/client_type_web_or_desktop_or_mobile_or_netbook/
    QString wsuri = QString("ws://%1:80/%2/desktop/").arg(this->m_ws_serv_ipaddr).arg(this->mSkype->handlerName());
    this->wscli = boost::shared_ptr<WebSocketClient>(new WebSocketClient(wsuri));
    QObject::connect(this->wscli.get(), SIGNAL(onConnected(QString)), this, SLOT(onWSConnected(QString)));
    QObject::connect(this->wscli.get(), SIGNAL(onDisconnected()), this, SLOT(onWSDisconnected()));
    QObject::connect(this->wscli.get(), SIGNAL(onWSMessage(QByteArray)), this, SLOT(onWSMessage(QByteArray)));
    QObject::connect(this->wscli.get(), SIGNAL(onError(int, const QString&)), 
                     this, SLOT(onWSError(int, const QString&)));
    bool ok = this->wscli->connectToServer();
    Q_ASSERT(ok);

    if (this->m_conn_ws_max_retry_times == this->m_conn_ws_max_retry_times) {

        QString log_msg = "连接预处理服务器。。。";
        this->log_output(LT_USER, log_msg);

        boost::shared_ptr<SqlRequest> req(new SqlRequest());

        req->mCbFunctor = boost::bind(&SkypePhone::onAddCallHistoryDone, this, _1);
        req->mCbObject = this;
        req->mCbSlot = SLOT(onAddCallHistoryDone(boost::shared_ptr<SqlRequest>));
        req->mSql = QString("INSERT INTO kp_histories (contact_id,phone_number,call_status, call_ctime) VALUES (%1, '%2', %3, '%4')")
            .arg(-1).arg(phone_number).arg(0)
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"));
        // req->mSql = QString("INSERT INTO kp_contacts (group_id,display_name,phone_number) VALUES (IFNULL((SELECT gid FROM kp_groups  WHERE group_name='%1'),3), '%2', '%3')")
        //     .arg(pc->mGroupName).arg(pc->mUserName).arg(pc->mPhoneNumber);
        
        req->mReqno = this->m_adb->execute(req->mSql);
        this->mRequests.insert(req->mReqno, req);

        qLogx()<<req->mSql;
    }
}

void SkypePhone::onHangupPstn()
{
    this->mSkype->setCallHangup(QString::number(this->m_curr_skype_call_id));
}

void SkypePhone::onShowDialPanel()
{
    // this->m_dialpanel_layout_item->widget()->setVisible(false);
    if (this->m_dialpanel_layout_item->widget()->isVisible()) {
        
    }
    // this->m_dialpanel_layout_item->widget()->setVisible(!this->m_dialpanel_layout_item->widget()->isVisible());
    this->onDynamicSetVisible(this->m_dialpanel_layout_item->widget(),
                              !this->m_dialpanel_layout_item->widget()->isVisible());
}

void SkypePhone::onShowLogPanel()
{
    // this->m_log_list_layout_item->widget()->setVisible(!this->m_log_list_layout_item->widget()->isVisible());
    this->m_log_list_widget->setVisible(!this->m_log_list_widget->isVisible());
}

/*
  正常流程，把信息取出来，号码显示在号码输入框，模拟点击呼叫按钮
 */
void SkypePhone::onPlaceCallFromContactList()
{
    qLogx()<<"";

    QItemSelectionModel *ism = this->uiw->treeView->selectionModel();

    if (!ism->hasSelection()) {
        return;
    }

    QModelIndex cidx, idx;
    cidx = ism->currentIndex();
    idx = ism->model()->index(cidx.row(), 1, cidx.parent());

    if (this->m_contact_model->hasChildren(idx)) {
        qLogx()<<"select node is group node";
        // group node
        return ;
    }
    
    ContactInfoNode *cnode = static_cast<ContactInfoNode*>(idx.internalPointer());
    
    QString phone_number = this->m_contact_model->data(idx).toString();
    qLogx()<<phone_number;

    this->uiw->comboBox_3->setEditText(phone_number);

    if (this->uiw->pushButton_4->isEnabled()) {
        this->uiw->pushButton_4->click();
    } else {
        
    }
}

void SkypePhone::onPlaceCallFromHistory()
{
    qLogx()<<"";

    QItemSelectionModel *ism = this->uiw->treeView_2->selectionModel();

    if (!ism->hasSelection()) {
        return;
    }

    QModelIndex cidx, idx;
    cidx = ism->currentIndex();
    idx = ism->model()->index(cidx.row(), 1, cidx.parent());

    QString phone_number = this->m_call_history_model->data(idx).toString();
    qLogx()<<phone_number;

    this->uiw->comboBox_3->setEditText(phone_number);

    if (this->uiw->pushButton_4->isEnabled()) {
        this->uiw->pushButton_4->click();
    } else {
        
    }
}

void SkypePhone::onDeleteCallHistory() 
{
    qLogx()<<"";

    QItemSelectionModel *ism = this->uiw->treeView_2->selectionModel();

    if (!ism->hasSelection()) {
        return;
    }

    QModelIndex cidx, idx, idx2;
    cidx = ism->currentIndex();
    idx = ism->model()->index(cidx.row(), 1, cidx.parent());
    idx2 = ism->model()->index(cidx.row(), 3, cidx.parent());

    QString phone_number = this->m_call_history_model->data(idx).toString();
    int hid = this->m_call_history_model->data(idx2).toInt();
    qLogx()<<hid<<phone_number;

    boost::shared_ptr<SqlRequest> req(new SqlRequest());

    req->mCbId = hid;
    req->mCbFunctor = boost::bind(&SkypePhone::onDeleteCallHistoryDone, this, _1);
    req->mCbObject = this;
    req->mCbSlot = SLOT(onDeleteCallHistoryDone(boost::shared_ptr<SqlRequest>));
    req->mSql = QString("DELETE FROM kp_histories WHERE hid=%1").arg(hid);
    req->mReqno = this->m_adb->execute(req->mSql);
    this->mRequests.insert(req->mReqno, req);

    // qLogx()<<req->mSql;
}

void SkypePhone::onDeleteAllCallHistory()
{
    qLogx()<<""<<this->m_call_history_model->rowCount();
    if (this->m_call_history_model->rowCount() == 0) {
        return;
    }

    boost::shared_ptr<SqlRequest> req(new SqlRequest());

    req->mCbFunctor = boost::bind(&SkypePhone::onDeleteAllCallHistoryDone, this, _1);
    req->mCbObject = this;
    req->mCbSlot = SLOT(onDeleteAllCallHistoryDone(boost::shared_ptr<SqlRequest>));
    req->mSql = QString("DELETE FROM kp_histories WHERE 1=1");
    req->mReqno = this->m_adb->execute(req->mSql);
    this->mRequests.insert(req->mReqno, req);

    // qLogx()<<req->mSql;
}

void SkypePhone::onAddContact()
{
    boost::shared_ptr<SqlRequest> req(new SqlRequest());
    // boost::shared_ptr<PhoneContact> pc;
    PhoneContact *pc;
    boost::scoped_ptr<PhoneContactProperty> pcp(new PhoneContactProperty(this));

    if (pcp->exec() == QDialog::Accepted) {
        pc = pcp->contactInfo();

        req->mCbFunctor = boost::bind(&SkypePhone::onAddContactDone, this, _1);
        req->mCbObject = this;
        req->mCbSlot = SLOT(onAddContactDone(boost::shared_ptr<SqlRequest>));
        // req->mSql = QString("INSERT INTO kp_contacts (group_id,phone_number) VALUES (%1, '%2')")
        //     .arg(pc->mGroupId).arg(pc->mPhoneNumber);
        req->mSql = QString("INSERT INTO kp_contacts (group_id,display_name,phone_number) VALUES (IFNULL((SELECT gid FROM kp_groups  WHERE group_name='%1'),3), '%2', '%3')")
            .arg(pc->mGroupName).arg(pc->mUserName).arg(pc->mPhoneNumber);
        req->mReqno = this->m_adb->execute(req->mSql);
        this->mRequests.insert(req->mReqno, req);

        qDebug()<<req->mSql;
    } else {

    }
}

void SkypePhone::onModifyContact()
{
    boost::shared_ptr<SqlRequest> req(new SqlRequest());
    // boost::shared_ptr<PhoneContact> pc;
    PhoneContact * pc;
    boost::scoped_ptr<PhoneContactProperty> pcp(new PhoneContactProperty(this));
    QItemSelectionModel *ism = this->uiw->treeView->selectionModel();

    if (!ism->hasSelection()) {
        return;
    }

    QModelIndex cidx, idx;
    cidx = ism->currentIndex();
    idx = ism->model()->index(cidx.row(), 0, cidx.parent());

    if (this->m_contact_model->hasChildren(idx)) {
        // group node
        return ;
    }
    
    ContactInfoNode *cnode = static_cast<ContactInfoNode*>(idx.internalPointer());
    pcp->setContactInfo(cnode->pc);

    if (pcp->exec() == QDialog::Accepted) {
        pc = pcp->contactInfo();

        req->mCbId = pc->mContactId;
        req->mCbFunctor = boost::bind(&SkypePhone::onModifyContactDone, this, _1);
        req->mCbObject = this;
        req->mCbSlot = SLOT(onModifyContactDone(boost::shared_ptr<SqlRequest>));
        req->mSql = QString("UPDATE kp_contacts SET group_id = (SELECT gid FROM kp_groups WHERE group_name='%1'), display_name='%2', phone_number='%3' WHERE cid='%4'")
            .arg(pc->mGroupName).arg(pc->mUserName).arg(pc->mPhoneNumber).arg(pc->mContactId);
        req->mReqno = this->m_adb->execute(req->mSql);
        this->mRequests.insert(req->mReqno, req);

        qDebug()<<req->mSql;
    } else {

    }
}

void SkypePhone::onAddGroup() 
{
    QString group_name;
    boost::shared_ptr<SqlRequest> req(new SqlRequest());
    boost::scoped_ptr<GroupInfoDialog> gidlg(new GroupInfoDialog(this));

    if (gidlg->exec() == QDialog::Accepted) {
        group_name = gidlg->groupName();

        req->mCbFunctor = boost::bind(&SkypePhone::onAddGroupDone, this, _1);
        req->mCbObject = this;
        req->mCbSlot = SLOT(onAddGroupDone(boost::shared_ptr<SqlRequest>));
        req->mSql = QString("INSERT INTO kp_groups (group_name) VALUES ('%1')")
            .arg(group_name);
        req->mReqno = this->m_adb->execute(req->mSql);
        this->mRequests.insert(req->mReqno, req);
    }
}

void SkypePhone::onShowContactViewMenu(const QPoint &pos)
{
    ContactInfoNode *cin = NULL;
    QModelIndex idx = this->uiw->treeView->indexAt(pos);
    // qDebug()<<idx<<idx.parent();
    if (idx.parent().isValid()) {
        // leaf contact node
    } else {

    }
    this->m_contact_view_ctx_menu->popup(this->uiw->treeView->mapToGlobal(pos));
}

void SkypePhone::onShowHistoryViewMenu(const QPoint &pos)
{
    ContactInfoNode *cin = NULL;
    QModelIndex idx = this->uiw->treeView_2->indexAt(pos);
    // qDebug()<<idx<<idx.parent();
    if (idx.parent().isValid()) {
        // leaf contact node
    } else {

    }
    this->m_history_view_ctx_menu->popup(this->uiw->treeView_2->mapToGlobal(pos));
}

void SkypePhone::onDynamicSetVisible(QWidget *w, bool visible)
{
    Q_ASSERT(w != NULL);

    this->mdyn_widget = w;
    this->mdyn_visible = visible;

    this->onDynamicSetVisible();
    // w->setWindowOpacity(0.5);
}

void SkypePhone::onDynamicSetVisible()
{
    int curr_opacity = 100;
    char *pname = "dyn_opacity";
    QVariant dyn_opacity = this->mdyn_widget->property(pname);

    if (dyn_opacity.isValid()) {
        curr_opacity = dyn_opacity.toInt();
        if (this->mdyn_visible == true) {
            curr_opacity += 10;
            this->mdyn_widget->setProperty(pname, QVariant(curr_opacity));
            if (curr_opacity == 100) {
                this->mdyn_widget->setVisible(true);
            } else {
                // this->mdyn_widget->setWindowOpacity(curr_opacity*1.0/100);
                this->mdyn_oe->setOpacity(curr_opacity*1.0/100);
                this->mdyn_widget->setGraphicsEffect(this->mdyn_oe);
                if (curr_opacity == 10) this->mdyn_widget->setVisible(true);
                QTimer::singleShot(50, this, SLOT(onDynamicSetVisible()));
            }
        } else {
            curr_opacity -= 10;
            this->mdyn_widget->setProperty(pname, QVariant(curr_opacity));
            if (curr_opacity == 0) {
                this->mdyn_widget->setVisible(false);
            } else {
                // this->mdyn_widget->setWindowOpacity(curr_opacity*1.0/100);
                this->mdyn_oe->setOpacity(curr_opacity*1.0/100);
                this->mdyn_widget->setGraphicsEffect(this->mdyn_oe);
                QTimer::singleShot(50, this, SLOT(onDynamicSetVisible()));
            }
        }
    } else {
        if (this->mdyn_visible == true) {
            curr_opacity = 0;
            this->mdyn_widget->setProperty(pname, QVariant(curr_opacity));
        } else {
            curr_opacity = 100;
            this->mdyn_widget->setProperty(pname, QVariant(curr_opacity));
        }
        QTimer::singleShot(1, this, SLOT(onDynamicSetVisible()));
    }
}


void SkypePhone::onWSConnected(QString path)
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<path;

    QString log_msg = "连接预处理服务器 OK。";
    this->log_output(LT_USER, log_msg);
    log_msg = "连接接线服务器。。。";
    this->log_output(LT_USER, log_msg);
    // 
    QString num = this->uiw->comboBox_3->currentText();
    QString cmd = QString("101$%1$%2").arg(this->mSkype->handlerName()).arg(num);
    this->wscli->sendMessage(cmd.toAscii());
    this->log_output(LT_DEBUG, cmd);
}

void SkypePhone::onWSError(int error, const QString &errmsg)
{
    qLogx()<<error<<errmsg;
    if (error == WebSocketClient::EWS_HANDSHAKE) {
        log_output(LT_USER, tr("网络协议错误，2秒后重试连接服务器。。。"));
        // 这种直接调用的情况下，被调用方法接收到的 sender()仍旧是调用处的sender()!!!
        // this->onCallPstn();
        QTimer::singleShot(100, this, SLOT(onCallPstn()));
    } else if (error == QAbstractSocket::RemoteHostClosedError) {
        // 通话完成？还是非正常连接中断？
    } else {
        log_output(LT_USER, tr("网络错误，3秒后重试连接服务器。。。") + errmsg);
        // this->onCallPstn();
        QTimer::singleShot(200, this, SLOT(onCallPstn()));
    }
}

void SkypePhone::onWSDisconnected()
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__;
    // 如果区别被服务器异常关闭，还是客户端主动关闭呢。
}

void SkypePhone::onWSMessage(QByteArray msg)
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<msg;
    
    QString router_name;
    QStringList tmps;
    QStringList fields = QString(msg).split("$");
    QString log_msg;

    switch (fields.at(0).toInt()) {
    case 102:
        Q_ASSERT(fields.at(1) == this->mSkype->handlerName());
        tmps = fields.at(2).split("\n");
        if (tmps.at(0).trimmed() == "200 OK") {
            Q_ASSERT(tmps.at(1).trimmed().toInt() >= 1);
            router_name = tmps.at(2).trimmed();
            
            this->mSkype->callFriend(router_name);
        }
        break;
    case 104:
        log_msg = "线路忙，请稍后再拨。";
        this->log_output(LT_USER, log_msg);

        // 是不是需要这些状态控制呢？ // 不需要，服务器端还会发送108通话结束指令。
        // this->onDynamicSetVisible(this->m_call_state_widget, false);
        // // 关闭网络连续，如果有的话。
        // this->wscli.reset();
        // this->uiw->pushButton_4->setEnabled(true);
        break;
    case 106:
        log_msg = QString("FCall notice: ") + fields.at(4);
        this->log_output(LT_DEBUG, log_msg);
        break;
    case 108:
        log_msg = "通话结束。";
        // log_msg = QString("<p><img src='%1'/> %2</p>").arg(":/skins/default/info.png").arg(log_msg);
        // this->uiw->plainTextEdit->appendPlainText(log_msg);
        // this->uiw->plainTextEdit->appendHtml(log_msg);
        this->log_output(LT_USER, log_msg);

        this->onDynamicSetVisible(this->m_call_state_widget, false);

        // 关闭网络连续，如果有的话。
        this->wscli.reset();
        this->uiw->pushButton_4->setEnabled(true);

        break;
    case 110:
        log_msg = "不支持通话挂起功能，请尽快恢复，否则对方可能因听不到您的声音而挂断。";
        this->log_output(LT_USER, log_msg);
        break;
    case 112:
        log_msg = "对方已经接通，计时开始。";
        this->log_output(LT_USER, log_msg);
        break;
    case 114:
        log_msg = "可能会有2-3秒静音时间，请稍后。。。"; // may be mute 3-5s
        this->log_output(LT_USER, log_msg);
        break;
    case 116:
        log_msg = "分配通话线路。。。";// Allocate circuitry...
        this->log_output(LT_USER, log_msg);
        break;
    case 117:
        log_msg = "连接对方话机。。。"; // Info: connect pstn network.
        this->log_output(LT_USER, log_msg);
        break;
    case 118:
        log_msg = QString("对方已挂机，代码:%1").arg(fields.at(5));
        // log_msg = QString("<p><img src='%1'/> %2</p>").arg(":/skins/default/info.png").arg(log_msg);
        // this->uiw->plainTextEdit->appendPlainText(log_msg);
        // this->uiw->plainTextEdit->appendHtml(log_msg);
        this->log_output(LT_USER, log_msg);

        switch (fields.at(5).toInt()) {
        case 603:
            break;
        default:
            break;
        };

        break;
    default:
        qDebug()<<"Unknwon ws msg:"<<msg;
        break;
    }

}

// TODO 在网络情况不好的情况下，确实有可能好3,5秒之后这个才返回
// 所以有必要在呼叫的时候检测这个值是否已经取到，要是没有取到，还需要等待操作完成。
// 需要两个值还控制呼叫按钮是否可用，有有点复杂了。
void SkypePhone::onCalcWSServByNetworkType(QHostInfo hi)
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<hi.addresses();

    QString log = "网络类型：";
    QString ws_serv_ipaddr;
    QList<QHostAddress> addrs = hi.addresses();
    
    if (addrs.count() > 0) {
        if (addrs.at(0).toString() == "202.108.15.80") {
            ws_serv_ipaddr = "202.108.15.81";
            log += "网通";
        } else if (addrs.at(0).toString() == "211.100.41.6") {
            ws_serv_ipaddr = "211.100.41.7";
            log += "电信";
        } else {
            qDebug()<<"You network is strange enought.";
            log += "网络异常，检测结果不准确";
            Q_ASSERT(1==2);
        }
        this->m_ws_serv_ipaddr = ws_serv_ipaddr;
    } else {
        qDebug()<<"Can not resolve IP for :" << hi.hostName();
        log += "网络异常，无法检测到网络类型";
    }


    // for test
    QString wsaddr = QSettings(QApplication::applicationDirPath() + "/kitphone.ini", QSettings::IniFormat)
        .value("wsaddr").toString().trimmed();
    if (!wsaddr.isEmpty()) {
        this->m_ws_serv_ipaddr = wsaddr;
        log += "(测试)";            
    }

    qLogx()<<"Network type:"<<this->m_ws_serv_ipaddr;

    // count == 0
    if (this->m_call_button_disable_count.deref() == false) {
        this->uiw->pushButton_4->setEnabled(true);
    }
    qDebug()<<"All in all, the notice server is:"<<this->m_ws_serv_ipaddr;
    this->log_output(LT_USER, log);

    ////////////// do network check now
    NetworkChecker::instance()->start();
}

void SkypePhone::onNoticeUserStartup()
{
    
}

void SkypePhone::onDatabaseConnected()
{
    // 加载联系人信息，加载呼叫历史记录信息
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__;
    boost::shared_ptr<SqlRequest> req1(new SqlRequest());
    boost::shared_ptr<SqlRequest> req2(new SqlRequest());
    boost::shared_ptr<SqlRequest> req3(new SqlRequest());

    this->uiw->treeView->setHeaderHidden(true);
    this->uiw->treeView->setModel(this->m_contact_model);
    this->uiw->treeView->setColumnHidden(2, false);
    this->uiw->treeView->setAnimated(true);
    // this->uiw->treeView->setIndentation(20);
    this->uiw->treeView->setColumnWidth(0, 120);
    this->uiw->treeView->setColumnWidth(1, 160);
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<this->uiw->treeView->columnWidth(0)
            <<this->uiw->treeView->columnWidth(1);

    this->uiw->treeView_2->setHeaderHidden(true);
    this->uiw->treeView_2->setModel(this->m_call_history_model);
    this->uiw->treeView->setColumnWidth(0, 100);
    this->uiw->treeView->setColumnWidth(1, 160);
    
    {
        // get contacts list
        req1->mCbFunctor = boost::bind(&SkypePhone::onGetAllContactsDone, this, _1);
        req1->mCbObject = this;
        req1->mCbSlot = SLOT(onGetAllContactsDone(boost::shared_ptr<SqlRequest>));
        req1->mSql = QString("SELECT * FROM kp_groups,kp_contacts WHERE kp_contacts.group_id=kp_groups.gid");
        req1->mReqno = this->m_adb->execute(req1->mSql);
        this->mRequests.insert(req1->mReqno, req1);
    }

    {
        // get groups list
        req2->mCbFunctor = boost::bind(&SkypePhone::onGetAllGroupsDone, this, _1);
        req2->mCbObject = this;
        req2->mCbSlot = SLOT(onGetAllGroupsDone(boost::shared_ptr<SqlRequest>));
        req2->mSql = QString("SELECT * FROM kp_groups");
        req2->mReqno = this->m_adb->execute(req2->mSql);
        this->mRequests.insert(req2->mReqno, req2);
    }

    {
        // get history list
        req3->mCbFunctor = boost::bind(&SkypePhone::onGetAllGroupsDone, this, _1);
        req3->mCbObject = this;
        req3->mCbSlot = SLOT(onGetAllGroupsDone(boost::shared_ptr<SqlRequest>));
        req3->mSql = QString("SELECT * FROM kp_histories ORDER BY call_ctime DESC");
        req3->mReqno = this->m_adb->execute(req3->mSql);
        this->mRequests.insert(req3->mReqno, req3);
    }

    /////
}

void SkypePhone::onSqlExecuteDone(const QList<QSqlRecord> & results, int reqno, bool eret, const QString &estr, const QVariant &eval)
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<reqno; 
    
    QObject *cb_obj = NULL;
    const char *cb_slot = NULL;
    boost::function<bool(boost::shared_ptr<SqlRequest>)> cb_functor;
    boost::shared_ptr<SqlRequest> req;
    bool bret = false;
    // QGenericReturnArgument qret;
    // QGenericArgument qarg;
    bool qret;
    QMetaMethod qmethod;
    char raw_method_name[32] = {0};
    int slot_idx = -1;

    if (this->mRequests.contains(reqno)) {
        req = this->mRequests[reqno];
        req->mRet = eret;
        req->mErrorString = estr;
        req->mExtraValue = eval;
        req->mResults = results;

        // 实现方法太多，还要随机使用一种方法，找麻烦
	// 第二种方式在windows7会出现崩溃
        // if (qrand() % 2 == 1) {
	if (1) {
            cb_functor = req->mCbFunctor;
            bret = cb_functor(req);
        } else {
            cb_obj = req->mCbObject;
            cb_slot = req->mCbSlot;

            qDebug()<<"qinvoke:"<<cb_obj<<cb_slot;
            // get method name from SLOT() signature: 1onAddContactDone(boost::shared_ptr<SqlRequest>)
            for (int i = 0, j = 0; i < strlen(cb_slot); ++i) {
                if (cb_slot[i] >= '0' && cb_slot[i] <= '9') {
                    continue;
                }
                if (cb_slot[i] == '(') break;
                Q_ASSERT(j < sizeof(raw_method_name));
                raw_method_name[j++] = cb_slot[i];
            }
            Q_ASSERT(strlen(raw_method_name) > 0);
	    slot_idx = cb_obj->metaObject()->indexOfSlot(raw_method_name);
	    qDebug()<<"qinvokde2:"<<slot_idx<<raw_method_name;
            // Q_ASSERT(cb_obj->metaObject()->indexOfSlot(raw_method_name) != -1);
            bret = QMetaObject::invokeMethod(cb_obj, raw_method_name,
                                             Q_RETURN_ARG(bool, qret),
                                             Q_ARG(boost::shared_ptr<SqlRequest>, req));
            // qmethod = cb_obj->metaObject()->method(cb_obj->metaObject()->indexOfSlot(SLOT(onAddContactDone(boost::shared_ptr<bret>))));
            // SqlRequest = qmethod.invoke(cb_obj, Q_RETURN_ARG(bool, qret),
            //                        Q_ARG(boost::shared_ptr<SqlRequest>, req));
            // qDebug()<<cb_obj->metaObject()->indexOfSlot(cb_slot);
        }
    }
}

bool SkypePhone::onAddContactDone(boost::shared_ptr<SqlRequest> req)
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<req->mReqno;    

    // qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<req->mExtraValue<<req->mErrorString<<req->mRet; 

    QList<QSqlRecord> recs;
    int ncid;

    if (!req->mRet) {
        this->log_output(LT_USER, "添加联系人出错：" + req->mErrorString);
    } else {
        ncid = req->mExtraValue.toInt();
        recs = req->mResults;

        this->m_contact_model->onContactsRetrived(recs.at(0).value("group_id").toInt(), recs);
    }
    
    this->mRequests.remove(req->mReqno);

    return true;
}

bool SkypePhone::onModifyContactDone(boost::shared_ptr<SqlRequest> req)
{
    
    this->m_contact_model->onContactModified(req->mCbId);
    return true;
}

bool SkypePhone::onAddGroupDone(boost::shared_ptr<SqlRequest> req)
{
    QList<QSqlRecord> recs;
    int ngid;

    if (!req->mRet) {
        this->log_output(LT_USER, "添加联系人组失败：" + req->mErrorString);
    } else {
        ngid = req->mExtraValue.toInt();
        recs = req->mResults;

        this->m_contact_model->onGroupsRetrived(recs);
    }

    qDebug()<<recs<<req->mExtraValue;

    this->mRequests.remove(req->mReqno);
    return true;
}

bool SkypePhone::onAddCallHistoryDone(boost::shared_ptr<SqlRequest> req)
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<req->mReqno;

    this->m_call_history_model->onNewCallHistoryArrived(req->mResults);    
    
    this->mRequests.remove(req->mReqno);
    return true;
}

bool SkypePhone::onGetAllContactsDone(boost::shared_ptr<SqlRequest> req)
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<req->mReqno;

    this->mRequests.remove(req->mReqno);
    return true;
}

bool SkypePhone::onGetAllGroupsDone(boost::shared_ptr<SqlRequest> req)
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<req->mReqno;

    this->mRequests.remove(req->mReqno);
    return true;
}

bool SkypePhone::onGetAllHistoryDone(boost::shared_ptr<SqlRequest> req)
{
    qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<req->mReqno;


    this->mRequests.remove(req->mReqno);
    return true;
}

bool SkypePhone::onDeleteCallHistoryDone(boost::shared_ptr<SqlRequest> req)
{
    qLogx()<<req->mReqno;

    this->m_call_history_model->onCallHistoryRemoved(req->mCbId);
    this->mRequests.remove(req->mReqno);
    return true;
}

bool SkypePhone::onDeleteAllCallHistoryDone(boost::shared_ptr<SqlRequest> req)
{
    qLogx()<<req->mReqno;

    this->m_call_history_model->onAllCallHistoryRemoved();
    this->mRequests.remove(req->mReqno);
    return true;
}


// TODO 所有明文字符串需要使用翻译方式获取，而不是直接写在源代码中
// log is utf8 codec
void SkypePhone::log_output(int type, const QString &log)
{
    QListWidgetItem *witem = nullptr;
    QString log_time = QDateTime::currentDateTime().toString("hh:mm:ss");

    int debug = 1;

    QTextCodec *u8codec = QTextCodec::codecForName("UTF-8");
    QString u16_log = log_time + " " + u8codec->toUnicode(log.toAscii());
    QString notime_log = u8codec->toUnicode(log.toAscii());

    if (type == LT_USER) {
        // TODO 怎么确定是属于呼叫日志呢。恐怕还是得在相应的地方执行才行。
        this->uiw->label_11->setText(notime_log);
        witem = new QListWidgetItem(QIcon(":/skins/default/info.png"), u16_log);
        this->uiw->listWidget->addItem(witem);
    } else if (type == LT_DEBUG && debug) {
        witem = new QListWidgetItem(QIcon(":/skins/default/info.png"), u16_log);
        this->uiw->listWidget->addItem(witem);
    } else {
        qDebug()<<__FILE__<<__LINE__<<__FUNCTION__<<type<<log;
    }

    // 清除多余日志
    static int max_log_count = 30;
    if (debug == 1) {
        max_log_count += 200;
    }
    if (this->uiw->listWidget->count() > max_log_count) {
        int rm_count = this->uiw->listWidget->count() - max_log_count;
        // 从最老的日志开始删除
        for (int i = 0; i < rm_count; i++) {
            witem = this->uiw->listWidget->takeItem(i);
            delete witem;
        }
        // 从最新的开始
        // for (int i = rm_count - 1; i >= 0; i--) {
        //     witem = this->uiw->listWidget->takeItem(max_log_count+i);
        //     delete witem;
        // }
    }

    this->uiw->listWidget->scrollToItem(this->uiw->listWidget->item(this->uiw->listWidget->count()-1));

    if (this->m_status_bar != nullptr) {
        this->m_status_bar->showMessage(u16_log);
    }
    qLogx()<<type<<u16_log;
}

