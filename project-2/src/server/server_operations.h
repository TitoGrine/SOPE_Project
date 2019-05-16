#ifndef SERVER_OPERATIONS_H
#define SERVER_OPERATIONS_H

#include "../sope.h"
#include "../constants.h"
#include "../debug.h"
#include "server_accounts.h"

int op_createAccount(req_value_t request_value, tlv_reply_t* reply, uint32_t officeID);
int op_checkBalance(req_value_t request_value, tlv_reply_t* reply, uint32_t officeID);
int op_transfer(req_value_t request_value, tlv_reply_t* reply, uint32_t officeID);
int op_shutdown(req_value_t request_value, tlv_reply_t* reply, uint32_t officeID);

#endif
