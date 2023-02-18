﻿#pragma warning(disable : 4251)

#include <memory>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#include <nng/nng.h>
#include <nng/protocol/pair1/pair.h>
#include <nng/supplemental/util/platform.h>

#include "wcf.pb.h"

#include "accept_new_friend.h"
#include "exec_sql.h"
#include "get_contacts.h"
#include "log.h"
#include "pb_types.h"
#include "pb_util.h"
#include "receive_msg.h"
#include "rpc_server.h"
#include "send_msg.h"
#include "spy.h"
#include "spy_types.h"
#include "util.h"

#define G_BUF_SIZE (16 * 1024 * 1024)

extern int IsLogin(void);    // Defined in spy.cpp
extern string GetSelfWxid(); // Defined in spy.cpp

bool gIsListening;
HANDLE g_hEvent; // New message signal
queue<WxMsg_t> gMsgQueue;

static DWORD lThreadId = 0;
static bool lIsRunning = false;
static nng_socket sock;
static uint8_t gBuffer[G_BUF_SIZE] = { 0 };

bool func_is_login(uint8_t *out, size_t *len)
{
    Response rsp   = Response_init_default;
    rsp.func       = Functions_FUNC_IS_LOGIN;
    rsp.which_msg  = Response_status_tag;
    rsp.msg.status = IsLogin();

    pb_ostream_t stream = pb_ostream_from_buffer(out, *len);
    if (!pb_encode(&stream, Response_fields, &rsp)) {
        LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
        return false;
    }
    *len = stream.bytes_written;

    return true;
}

bool func_get_self_wxid(uint8_t *out, size_t *len)
{
    Response rsp  = Response_init_default;
    rsp.func      = Functions_FUNC_IS_LOGIN;
    rsp.which_msg = Response_str_tag;
    rsp.msg.str   = (char *)GetSelfWxid().c_str();

    pb_ostream_t stream = pb_ostream_from_buffer(out, *len);
    if (!pb_encode(&stream, Response_fields, &rsp)) {
        LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
        return false;
    }
    *len = stream.bytes_written;

    return true;
}

bool func_get_msg_types(uint8_t *out, size_t *len)
{
    Response rsp  = Response_init_default;
    rsp.func      = Functions_FUNC_GET_MSG_TYPES;
    rsp.which_msg = Response_types_tag;

    MsgTypes_t types                 = GetMsgTypes();
    rsp.msg.types.types.funcs.encode = encode_types;
    rsp.msg.types.types.arg          = &types;

    pb_ostream_t stream = pb_ostream_from_buffer(out, *len);
    if (!pb_encode(&stream, Response_fields, &rsp)) {
        LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
        return false;
    }
    *len = stream.bytes_written;

    return true;
}

bool func_get_contacts(uint8_t *out, size_t *len)
{
    Response rsp  = Response_init_default;
    rsp.func      = Functions_FUNC_GET_CONTACTS;
    rsp.which_msg = Response_contacts_tag;

    vector<RpcContact_t> contacts          = GetContacts();
    rsp.msg.contacts.contacts.funcs.encode = encode_contacts;
    rsp.msg.contacts.contacts.arg          = &contacts;

    pb_ostream_t stream = pb_ostream_from_buffer(out, *len);
    if (!pb_encode(&stream, Response_fields, &rsp)) {
        LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
        return false;
    }
    *len = stream.bytes_written;

    return true;
}

bool func_get_db_names(uint8_t *out, size_t *len)
{
    Response rsp  = Response_init_default;
    rsp.func      = Functions_FUNC_GET_DB_NAMES;
    rsp.which_msg = Response_dbs_tag;

    DbNames_t dbnames              = GetDbNames();
    rsp.msg.dbs.names.funcs.encode = encode_dbnames;
    rsp.msg.dbs.names.arg          = &dbnames;

    pb_ostream_t stream = pb_ostream_from_buffer(out, *len);
    if (!pb_encode(&stream, Response_fields, &rsp)) {
        LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
        return false;
    }
    *len = stream.bytes_written;

    return true;
}

bool func_get_db_tables(char *db, uint8_t *out, size_t *len)
{
    Response rsp  = Response_init_default;
    rsp.func      = Functions_FUNC_GET_DB_TABLES;
    rsp.which_msg = Response_tables_tag;

    DbTables_t tables                  = GetDbTables(db);
    rsp.msg.tables.tables.funcs.encode = encode_tables;
    rsp.msg.tables.tables.arg          = &tables;

    pb_ostream_t stream = pb_ostream_from_buffer(out, *len);
    if (!pb_encode(&stream, Response_fields, &rsp)) {
        LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
        return false;
    }
    *len = stream.bytes_written;

    return true;
}

bool func_send_txt(TextMsg txt, uint8_t *out, size_t *len)
{
    Response rsp   = Response_init_default;
    rsp.func       = Functions_FUNC_SEND_TXT;
    rsp.which_msg  = Response_status_tag;
    rsp.msg.status = 0;

    if ((txt.msg == NULL) || (txt.receiver == NULL)) {
        rsp.msg.status = -1; // Empty message or empty receiver
    } else {
        string msg(txt.msg);
        string receiver(txt.receiver);
        string aters(txt.aters ? txt.aters : "");

        SendTextMessage(receiver, msg, aters);
    }

    pb_ostream_t stream = pb_ostream_from_buffer(out, *len);
    if (!pb_encode(&stream, Response_fields, &rsp)) {
        LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
        return false;
    }
    *len = stream.bytes_written;

    return true;
}

bool func_send_img(char *path, char *receiver, uint8_t *out, size_t *len)
{
    Response rsp   = Response_init_default;
    rsp.func       = Functions_FUNC_SEND_IMG;
    rsp.which_msg  = Response_status_tag;
    rsp.msg.status = 0;

    if ((path == NULL) || (receiver == NULL)) {
        rsp.msg.status = -1;
    } else {
        SendImageMessage(receiver, path);
    }

    pb_ostream_t stream = pb_ostream_from_buffer(out, *len);
    if (!pb_encode(&stream, Response_fields, &rsp)) {
        LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
        return false;
    }
    *len = stream.bytes_written;

    return true;
}

static void PushMessage()
{
    static uint8_t buffer[1024] = { 0 };

    int rv;
    Response rsp  = Response_init_default;
    rsp.func      = Functions_FUNC_ENABLE_RECV_TXT;
    rsp.which_msg = Response_wxmsg_tag;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, 1024);

    while (gIsListening) {
        // 中断式，兼顾及时性和CPU使用率
        rv = WaitForSingleObject(g_hEvent, 1000); // 等待消息，每秒检查一次条件
        if (rv == WAIT_TIMEOUT) {
            continue;
        } else if (rv != WAIT_OBJECT_0) {
            LOG_INFO("WaitForSingleObject ERRIR[{}]: {}", rv, GetLastError());
            continue;
        }

        while (!gMsgQueue.empty()) {
            auto wxmgs = gMsgQueue.front();
            rsp.msg.wxmsg.is_self  = wxmgs.is_self;
            rsp.msg.wxmsg.is_group = wxmgs.is_group;
            rsp.msg.wxmsg.type     = wxmgs.type;
            rsp.msg.wxmsg.id       = (char *)wxmgs.id.c_str();
            rsp.msg.wxmsg.xml      = (char *)wxmgs.xml.c_str();
            rsp.msg.wxmsg.sender   = (char *)wxmgs.sender.c_str();
            rsp.msg.wxmsg.roomid   = (char *)wxmgs.roomid.c_str();
            rsp.msg.wxmsg.content  = (char *)wxmgs.content.c_str();
            gMsgQueue.pop();

            if (!pb_encode(&stream, Response_fields, &rsp)) {
                LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
                continue;
            }
            rv = nng_send(sock, buffer, stream.bytes_written, 0);
            if (rv != 0) {
                LOG_ERROR("nng_send: {}", rv);
            }
        }
        ResetEvent(g_hEvent);
    }
}

bool func_enable_recv_txt(uint8_t *out, size_t *len)
{
    Response rsp   = Response_init_default;
    rsp.func       = Functions_FUNC_ENABLE_RECV_TXT;
    rsp.which_msg  = Response_status_tag;
    rsp.msg.status = -1;

    g_hEvent = CreateEvent(NULL,  // default security attributes
                           TRUE,  // manual-reset event
                           FALSE, // initial state is nonsignaled
                           NULL   // unnamed
    );
    if (g_hEvent == NULL) {
        LOG_ERROR("CreateMutex error: {}", GetLastError());
        return false;
    }

    ListenMessage();
    HANDLE msgThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)PushMessage, NULL, NULL, NULL);
    if (msgThread != 0) {
        CloseHandle(msgThread);
        rsp.msg.status = 0;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(out, *len);
    if (!pb_encode(&stream, Response_fields, &rsp)) {
        LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
        return false;
    }
    *len = stream.bytes_written;

    return true;
}

bool func_disable_recv_txt(uint8_t *out, size_t *len)
{
    Response rsp   = Response_init_default;
    rsp.func       = Functions_FUNC_DISABLE_RECV_TXT;
    rsp.which_msg  = Response_status_tag;
    rsp.msg.status = 0;

    UnListenMessage(); // 可能需要1秒之后才能退出，见 PushMessage

    pb_ostream_t stream = pb_ostream_from_buffer(out, *len);
    if (!pb_encode(&stream, Response_fields, &rsp)) {
        LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
        return false;
    }
    *len = stream.bytes_written;

    return true;
}

bool func_exec_db_query(char *db, char *sql, uint8_t *out, size_t *len)
{
    Response rsp  = Response_init_default;
    rsp.func      = Functions_FUNC_GET_DB_TABLES;
    rsp.which_msg = Response_rows_tag;

    DbRows_t rows                  = ExecDbQuery(db, sql);
    rsp.msg.rows.rows.arg          = &rows;
    rsp.msg.rows.rows.funcs.encode = encode_rows;

    pb_ostream_t stream = pb_ostream_from_buffer(out, *len);
    if (!pb_encode(&stream, Response_fields, &rsp)) {
        LOG_ERROR("Encoding failed: {}", PB_GET_ERROR(&stream));
        return false;
    }
    *len = stream.bytes_written;

    return true;
}

static bool dispatcher(uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len)
{
    bool ret            = false;
    Request req         = Request_init_default;
    pb_istream_t stream = pb_istream_from_buffer(in, in_len);
    if (!pb_decode(&stream, Request_fields, &req)) {
        LOG_ERROR("Decoding failed: {}", PB_GET_ERROR(&stream));
        pb_release(Request_fields, &req);
        return false;
    }

    LOG_INFO("Func: {}", (uint8_t)req.func);
    switch (req.func) {
        case Functions_FUNC_IS_LOGIN: {
            LOG_INFO("[Functions_FUNC_IS_LOGIN]");
            ret = func_is_login(out, out_len);
            break;
        }
        case Functions_FUNC_GET_SELF_WXID: {
            LOG_INFO("[Functions_FUNC_GET_SELF_WXID]");
            ret = func_get_self_wxid(out, out_len);
            break;
        }
        case Functions_FUNC_GET_MSG_TYPES: {
            LOG_INFO("[Functions_FUNC_GET_MSG_TYPES]");
            ret = func_get_msg_types(out, out_len);
            break;
        }
        case Functions_FUNC_GET_CONTACTS: {
            LOG_INFO("[Functions_FUNC_GET_CONTACTS]");
            ret = func_get_contacts(out, out_len);
            break;
        }
        case Functions_FUNC_GET_DB_NAMES: {
            LOG_INFO("[Functions_FUNC_GET_DB_NAMES]");
            ret = func_get_db_names(out, out_len);
            break;
        }
        case Functions_FUNC_GET_DB_TABLES: {
            LOG_INFO("[Functions_FUNC_GET_DB_TABLES]");
            ret = func_get_db_tables(req.msg.str, out, out_len);
            break;
        }
        case Functions_FUNC_SEND_TXT: {
            LOG_INFO("[Functions_FUNC_SEND_TXT]");
            ret = func_send_txt(req.msg.txt, out, out_len);
            break;
        }
        case Functions_FUNC_SEND_IMG: {
            LOG_INFO("[Functions_FUNC_SEND_IMG]");
            ret = func_send_img(req.msg.img.path, req.msg.txt.receiver, out, out_len);
            break;
        }
        case Functions_FUNC_ENABLE_RECV_TXT: {
            LOG_INFO("[Functions_FUNC_ENABLE_RECV_TXT]");
            ret = func_enable_recv_txt(out, out_len);
            break;
        }
        case Functions_FUNC_DISABLE_RECV_TXT: {
            LOG_INFO("[Functions_FUNC_DISABLE_RECV_TXT]");
            ret = func_disable_recv_txt(out, out_len);
            break;
        }
        case Functions_FUNC_EXEC_DB_QUERY: {
            LOG_INFO("[Functions_FUNC_EXEC_DB_QUERY]");
            ret = func_exec_db_query(req.msg.query.db, req.msg.query.sql, out, out_len);
            break;
        }
        default: {
            LOG_ERROR("[UNKNOW FUNCTION]");
            break;
        }
    }
    pb_release(Request_fields, &req);
    return ret;
}

static int RunServer()
{
    int rv    = 0;
    char *url = (char *)"tcp://0.0.0.0:10086";
    if ((rv = nng_pair1_open(&sock)) != 0) {
        LOG_ERROR("nng_pair0_open error {}", rv);
        return rv;
    }

    if ((rv = nng_listen(sock, url, NULL, 0)) != 0) {
        LOG_ERROR("nng_listen error {}", rv);
        return rv;
    }

    LOG_INFO("Server listening on {}", url);
    if ((rv = nng_setopt_ms(sock, NNG_OPT_SENDTIMEO, 1000)) != 0) {
        LOG_ERROR("nng_recv: {}", rv);
        return rv;
    }

    lIsRunning = true;
    while (lIsRunning) {
        uint8_t *in = NULL;
        size_t in_len, out_len = G_BUF_SIZE;
        if ((rv = nng_recv(sock, &in, &in_len, NNG_FLAG_ALLOC)) != 0) {
            LOG_ERROR("nng_recv: {}", rv);
            break;
        }

        log_buffer(in, in_len);
        if (dispatcher(in, in_len, gBuffer, &out_len)) {
            LOG_INFO("Send data length {}", out_len);
            // log_buffer(gBuffer, out_len);
            rv = nng_send(sock, gBuffer, out_len, 0);
            if (rv != 0) {
                LOG_ERROR("nng_send: {}", rv);
            }

        } else {
            // Error
            LOG_ERROR("Dispatcher failed...");
            rv = nng_send(sock, gBuffer, 0, 0);
            // break;
        }
        nng_free(in, in_len);
    }
    LOG_INFO("Leave RunServer");
    return rv;
}

int RpcStartServer()
{
    if (lIsRunning) {
        return 0;
    }

    HANDLE rpcThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)RunServer, NULL, NULL, &lThreadId);
    if (rpcThread != 0) {
        CloseHandle(rpcThread);
    }

    return 0;
}

int RpcStopServer()
{
    if (lIsRunning) {
        nng_close(sock);
        UnListenMessage();
        lIsRunning = false;
        Sleep(1000);
        LOG_INFO("Server stoped.");
    }
    return 0;
}
