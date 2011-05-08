// sipphone.h --- 
// 
// Author: liuguangzhao
// Copyright (C) 2007-2010 liuguangzhao@users.sf.net
// URL: 
// Created: 2011-04-18 21:30:38 +0800
// Version: $Id: sipphone.h 874 2011-05-08 08:48:25Z drswinghead $
// 

#ifndef _SIPPHONE_H_
#define _SIPPHONE_H_

#include <QtCore>
#include <QtNetwork>
#include <QtGui>
#include <QtSql>

#include "boost/signals2.hpp"
#include "boost/smart_ptr.hpp"

extern "C" {
	#include <pjlib.h>
	#include <pjlib-util.h>
	#include <pjmedia.h>
	#include <pjmedia-codec.h>
	#include <pjsip.h>
	#include <pjsip_simple.h>
	#include <pjsip_ua.h>
	#include <pjsua-lib/pjsua.h>
}

#define VOIP_SERVER "voip.qtchina.net"
#define SIP_SERVER "sip.qtchina.net"
// #define TURN_SERVER "turn.qtchina.net"
#define TURN_SERVER "turn.qtchina.net"
#define TURN_PORT "34780"

class SipAccountList;
class PjsipCallFront;
class AsyncDatabase;
class ContactModel;
class CallHistoryModel;
class SqlRequest;

namespace Ui {
    class SipPhone;
};

// 还要让它处理异步来自主界面线程的sip调用
class PJSipEventThread : public QThread
{
    Q_OBJECT;
public: 
    explicit PJSipEventThread(QObject *parent = 0);
    virtual ~PJSipEventThread();
    
    void run();

    void dump_info(pj_thread_t *thread);

private:
    bool quit_loop;
};

class SipPhone : public QWidget
{
    Q_OBJECT;
public:
    explicit SipPhone(QWidget *parent = 0);
    virtual ~SipPhone();

public slots: // sip
    void defaultSipInit();
    void on3_invoker_started(pj_status_t status);

    // ui
    void main_ui_draw_complete();

    void init_sip_client_ui_element();
    void onManageSipAccounts();
    void onRegisterAccount(QString user_name, bool reg);
    void onRemoveAccount(QString user_name);
    void onCallSip();
    void onHangupSip();
    
    void onCallSipNew();
    void onHangupSipNew();

    void onSelectedUserAccountChanged(int idx);
    void onDigitButtonClicked();
    
    // sip
    void on1_call_state(pjsua_call_id call_id, pjsip_event *e, pjsua_call_info *pci);
    void on1_call_media_state(pjsua_call_id call_id, pjsua_call_info *pci);
    void on1_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id, pjsip_rx_data *rdata);

    // void on1_new_connection(void *m_port);

    // void on1_put_frame(QTcpSocket *sock, QByteArray fba);
    
    void on2_pjsua_start_done(int seqno, pj_status_t status);
    void on2_make_call_done(int seqno, pj_status_t status, pjsua_call_id call_id);

    void onDatabaseConnected();
    // database exec callbacks
    void onSqlExecuteDone(const QList<QSqlRecord> & results, int reqno, bool eret, 
                  const QString &estr, const QVariant &eval);

    void onAddContact();
    void onModifyContact();
    void onAddGroup();

    void onShowDialPanel();
    void onShowLogPanel();
    void onDynamicSetVisible(QWidget *w, bool visible);
    void onDynamicSetVisible();

    void customAddContactButtonMenu();
    void initContactViewContextMenu();
    void onShowContactViewMenu(const QPoint &pos);

    bool onAddContactDone(boost::shared_ptr<SqlRequest> req);
    bool onModifyContactDone(boost::shared_ptr<SqlRequest> req);
    bool onAddGroupDone(boost::shared_ptr<SqlRequest> req);
    bool onAddCallHistoryDone(boost::shared_ptr<SqlRequest> req);

    bool onGetAllContactsDone(boost::shared_ptr<SqlRequest> req);
    bool onGetAllGroupsDone(boost::shared_ptr<SqlRequest> req);
    bool onGetAllHistoryDone(boost::shared_ptr<SqlRequest> req);

    void log_output(int type, const QString &log);

private slots:
    void set_custom_sip_config();

public:
    enum LOGTYPE {
        LT_USER = 0,
        LT_DEBUG = 1
    };

private:
    pjsua_acc_id _create_sip_account(QString acc_name);
    QString _get_sip_from_domain();
    int _find_account_from_pjacc(QString acc_name);

protected:
    bool first_paint_event;
    virtual void 	paintEvent ( QPaintEvent * event );
    virtual void 	showEvent ( QShowEvent * event );

private: // sip
    SipAccountList *acc_list;
    pjsua_config m_ua_cfg;
    pjsua_logging_config m_log_cfg;
    pjsua_media_config m_media_cfg;
    pjsua_transport_config m_tcp_tp_cfg;
    pjsua_transport_config m_udp_tp_cfg;
    pjsua_transport_config m_ipv6_tp_cfg;
    pjsua_transport_config m_tls_tp_cfg;
    pjsua_transport_config m_tcp6_tp_cfg;
    pjsua_transport_config m_udp6_tp_cfg;

    pjsua_transport_id m_udp_tp_id;
    pjsua_transport_id m_tcp_tp_id;
    pjsua_transport_id tls_tpid;
    pjsua_transport_id ipv6_tpid;
    pjsua_transport_id udp6_tpid;
    pjsua_transport_id tcp6_tpid;
    pjsua_transport_id user_tpid;
    pjsua_transport_id sctp_tpid;

    PjsipCallFront *m_invoker;
    pjsua_call_id m_curr_call_id;

    ///////////////////////
    QWidget *mdyn_widget;
    bool mdyn_visible;
    QGraphicsOpacityEffect *mdyn_oe;

    QStatusBar *m_status_bar;
    int m_dialpanel_layout_index;
    QLayoutItem *m_dialpanel_layout_item;
    int m_call_state_layout_index;
    QLayoutItem *m_call_state_layout_item;
    int m_log_list_layout_index;
    QLayoutItem *m_log_list_layout_item;

    QMenu *m_contact_view_ctx_menu;

    // AsyncDatabase *m_adb;
    boost::shared_ptr<AsyncDatabase> m_adb;
    ContactModel *m_contact_model;

    CallHistoryModel *m_call_history_model;

    //// sql reqno <---> sql reqclass
    QHash<int, boost::shared_ptr<SqlRequest> > mRequests;
    
private:
    Ui::SipPhone *uiw;
};

#endif /* _SIPPHONE_H_ */
