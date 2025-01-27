#include "server_bank_office.h"
#include <semaphore.h>

int request_fifo_fd;
int request_fifo_fd_DUMMY;

bool shutdown;

sem_t empty;
sem_t full;
pthread_mutex_t request_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t thread_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
bank_office_t offices[MAX_BANK_OFFICES+1];

int total_thread_cnt;
int active_thread_cnt;

int createBankOffices(unsigned int quantity)
{

    offices[0].id = MAIN_THREAD_ID;
    offices[0].tid = pthread_self();
    shutdown = false;
    total_thread_cnt = quantity;
    active_thread_cnt = 0;

    for (unsigned int i = 1; i <= quantity; i++)
    {
        offices[i].id = i;
        if (pthread_create(&offices[i].tid, NULL, bank_office_service_routine, (void *)&offices[i]) != 0)
            return -1;
    }

    return 0;
}

void *bank_office_service_routine(void *officePtr)
{
    bank_office_t *office = (bank_office_t *)officePtr;
    logBankOfficeOpen(getLogfile(), office->id, office->tid);

    int semValue;

    while (true)
    {

        sem_getvalue(&full, &semValue);
        logSyncMechSem(getLogfile(), office->id, SYNC_OP_SEM_WAIT, SYNC_ROLE_CONSUMER, 0, semValue);

        sem_wait(&full);

        pthread_mutex_lock(&request_queue_mutex);

        if(shutdown && queue_is_empty(&requests)){

            pthread_mutex_unlock(&request_queue_mutex);
            break;
        }

        pthread_mutex_unlock(&request_queue_mutex);



        pthread_mutex_lock(&thread_cnt_mutex);
        active_thread_cnt++;
        pthread_mutex_unlock(&thread_cnt_mutex);

        logSyncMech(getLogfile(), office->id, SYNC_OP_MUTEX_LOCK, SYNC_ROLE_CONSUMER, 0);
        pthread_mutex_lock(&request_queue_mutex);

        tlv_request_t currentRequest = queue_pop(&requests);

        logRequest(getLogfile(), office->id, &currentRequest);

        pthread_mutex_unlock(&request_queue_mutex);
        logSyncMech(getLogfile(), office->id, SYNC_OP_MUTEX_UNLOCK, SYNC_ROLE_CONSUMER, currentRequest.value.header.pid);

        sem_post(&empty);

        sem_getvalue(&empty, &semValue);
        logSyncMechSem(getLogfile(), office->id, SYNC_OP_SEM_POST, SYNC_ROLE_CONSUMER, currentRequest.value.header.pid, semValue);

        handleRequest(currentRequest, office->id);

        pthread_mutex_lock(&thread_cnt_mutex);
        active_thread_cnt--;
        pthread_mutex_unlock(&thread_cnt_mutex);
    }

    return officePtr;
}

int checkRequestHeader(tlv_request_t req)
{

    if (!existsBankAccount(req.value.header.account_id))
        return -1;

    if (!passwordIsCorrect(*findBankAccount(req.value.header.account_id), req.value.header.password))
        return -1;

    if (((req.type == OP_CREATE_ACCOUNT) && (req.value.header.account_id != ADMIN_ACCOUNT_ID)) ||
        ((req.type == OP_BALANCE) && (req.value.header.account_id == ADMIN_ACCOUNT_ID)) ||
        ((req.type == OP_TRANSFER) && (req.value.header.account_id == ADMIN_ACCOUNT_ID || req.value.transfer.account_id == ADMIN_ACCOUNT_ID)) ||
        ((req.type == OP_SHUTDOWN) && (req.value.header.account_id != ADMIN_ACCOUNT_ID)))
    {

        return -2;
    }

    return 0;
}

bool passwordIsCorrect(bank_account_t account, char *pwd)
{

    char temp[MAX_PASSWORD_LEN + SALT_LEN];

    strcpy(temp, pwd);
    strcat(temp, account.salt);

    if (generateSHA256sum(temp, temp) != 0)
        return false;

    if (strcmp(temp, account.hash) == 0)
        return true;
    else
        return false;
}

int getActiveThreadCount()
{
    return active_thread_cnt;
}

int handleRequest(tlv_request_t request, uint32_t officeID)
{

    enum op_type type = request.type;

    tlv_reply_t reply;
    reply.value.header.account_id = request.value.header.account_id;
    reply.type = type;

    int headerCheckStatus = checkRequestHeader(request);
    // TODO: METER EM SECCAO CRITICA

    if (headerCheckStatus != 0)
    {
        if (headerCheckStatus == -1)
            reply.value.header.ret_code = RC_LOGIN_FAIL;
        if (headerCheckStatus == -2)
            reply.value.header.ret_code = RC_OP_NALLOW;

        reply.length = sizeof(rep_header_t);
    }
    else
    {
        switch (type)
        {
        case OP_CREATE_ACCOUNT:

            if (op_createAccount(request.value, &reply, officeID) != 0)
            {
                reply.value.header.ret_code = RC_OTHER;
                reply.length = sizeof(rep_header_t);
            }

            break;

        case OP_BALANCE:

            if (op_checkBalance(request.value, &reply, officeID) != 0)
            {

                reply.value.header.ret_code = RC_OTHER;
                reply.length = sizeof(rep_header_t);
            }

            break;

        case OP_TRANSFER:

            if (op_transfer(request.value, &reply, officeID) != 0)
            {

                reply.value.header.ret_code = RC_OTHER;
                reply.length = sizeof(rep_header_t);
            }

            break;

        case OP_SHUTDOWN:

            if (op_shutdown(request.value, &reply, officeID) != 0)
            {

                reply.value.header.ret_code = RC_OTHER;
                reply.length = sizeof(rep_header_t);
            }
            else
            {

                logDelay(getLogfile(), officeID, request.value.header.op_delay_ms);
                usleep(MS_TO_US(request.value.header.op_delay_ms));
                shutdown_server();

                if (reply.value.header.ret_code != OK)
                    reply.value.shutdown.active_offices = 0;
                else
                    reply.value.shutdown.active_offices = getActiveThreadCount() - 1; // the -1 is to account for the thread which is handling the shutdown
            }

            break;

        default:
            break;
        }
    }

    if (sendReply(request, reply, officeID) != 0)
        return -1;

    return 0;
}

int sendReply(tlv_request_t request, tlv_reply_t reply, uint32_t officeID)
{

    char user_id[WIDTH_ID + 1];
    char reply_fifo_name[USER_FIFO_PATH_LEN] = USER_FIFO_PATH_PREFIX;
    sprintf(user_id, "%05d", request.value.header.pid);
    strcat(reply_fifo_name, user_id);

    int reply_fifo_fd = open(reply_fifo_name, O_WRONLY);

    if (reply_fifo_fd == -1)
    {   
        if (reply.value.header.ret_code != RC_OK)
            reply.value.header.ret_code = RC_USR_DOWN;
    }
    else if (write(reply_fifo_fd, &reply, sizeof(op_type_t) + sizeof(uint32_t) + reply.length) == -1)
    {

        perror("Reply write error");
    }

    logReply(getLogfile(), officeID, &reply);
    close(reply_fifo_fd);

    return 0;
}

int setupRequestFIFO()
{
    if (mkfifo(SERVER_FIFO_PATH, REQUEST_FIFO_PERM) == -1)
    {

        perror("Request FIFO creation error");
        return -1;
    }

    request_fifo_fd = open(SERVER_FIFO_PATH, O_RDONLY);

    if (request_fifo_fd == -1)
    {

        perror("Request FIFO opening error");
        return -2;
    }

    request_fifo_fd_DUMMY = open(SERVER_FIFO_PATH, O_WRONLY);

    if (request_fifo_fd_DUMMY == -1)
    {

        perror("Request FIFO opening error");
        close(request_fifo_fd);
        return -3;
    }

    return 0;
}

int waitForRequests()
{

    int semValue, nRead;
    bool isEOF = false;

    tlv_request_t received_request;

    while (!shutdown || !isEOF)
    {

        nRead = read(request_fifo_fd, &received_request.type, sizeof(op_type_t));

        if (nRead == 0)
            break;
        else if (nRead == -1)
            continue;

        nRead = read(request_fifo_fd, &received_request.length, sizeof(uint32_t));

        if (nRead == 0)
            break;
        else if (nRead == -1)
            continue;

        nRead = read(request_fifo_fd, &received_request.value, received_request.length);

        if (nRead == 0)
            break;
        else if (nRead == -1)
            continue;

        logRequest(getLogfile(), MAIN_THREAD_ID, &received_request);
        sem_getvalue(&empty, &semValue);
        logSyncMechSem(getLogfile(), MAIN_THREAD_ID, SYNC_OP_SEM_WAIT, SYNC_ROLE_PRODUCER, received_request.value.header.pid, semValue);
        sem_wait(&empty);

        logSyncMech(getLogfile(), MAIN_THREAD_ID, SYNC_OP_MUTEX_LOCK, SYNC_ROLE_PRODUCER, received_request.value.header.pid);
        pthread_mutex_lock(&request_queue_mutex);

        queue_push(&requests, received_request);

        pthread_mutex_unlock(&request_queue_mutex);
        logSyncMech(getLogfile(), MAIN_THREAD_ID, SYNC_OP_MUTEX_UNLOCK, SYNC_ROLE_PRODUCER, received_request.value.header.pid);

        sem_post(&full);
        sem_getvalue(&full, &semValue);
        logSyncMechSem(getLogfile(), MAIN_THREAD_ID, SYNC_OP_SEM_POST, SYNC_ROLE_PRODUCER, received_request.value.header.pid, semValue);
    }

    return closeCommunication();
}

int shutdown_server()
{
    shutdown = true;
    if(fchmod(request_fifo_fd, S_IRUSR | S_IRGRP | S_IROTH) == -1){

        perror("Request fifo permission mod error");
    }
    
    if(close(request_fifo_fd_DUMMY) == -1){

        perror("Request fifo closing error");
        return -1;
    }

    return 0;
}

int closeOffices()
{

    for(int i = 1; i <= total_thread_cnt;i++){

        sem_post(&full);
    }

    for (int i = 1; i <= total_thread_cnt; i++)
    {

        pthread_join(offices[i].tid,NULL);
        logBankOfficeClose(getLogfile(), offices[i].id, offices[i].tid);
    }

    return 0;
}

int closeCommunication() {

    sem_destroy(&full);
    sem_destroy(&empty);

    int returnCode = 0;

    if(close(request_fifo_fd) == -1){

        perror("Request fifo closing error");
        returnCode = -1;
    }

    if(unlink(SERVER_FIFO_PATH) == -1){

        perror("Request fifo unlinking error");
        returnCode = -2;
    }

    return returnCode;
}