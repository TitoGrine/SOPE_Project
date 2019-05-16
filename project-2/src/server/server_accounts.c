#include "server_accounts.h"

static bank_account_t accounts[MAX_BANK_ACCOUNTS];
pthread_mutex_t account_mutex[MAX_BANK_ACCOUNTS];


bank_account_t createBankAccount(uint32_t id, char *password, uint32_t balance)
{
    // TODO: criar contas com id repetido está a responder OTHER em vez de RC_ID_IN_USE - checkar isto
    bank_account_t newBankAccount;

    newBankAccount.account_id = id;
    newBankAccount.balance = balance;
    generateSalt(newBankAccount.salt);

    char temp[MAX_PASSWORD_LEN + SALT_LEN];

    strcpy(temp, password);
    strcat(temp, newBankAccount.salt);

    generateSHA256sum(temp, newBankAccount.hash);

    return newBankAccount;
}

bank_account_t createAdminBankAccount(char *password)
{
    return createBankAccount(ADMIN_ACCOUNT_ID, password, 0);
}

int insertBankAccount(bank_account_t newAccount, uint32_t delay,uint32_t officeID)
{
    if (existsBankAccount(newAccount.account_id)) 
        return RC_ID_IN_USE;

    logSyncMech(getLogfile(),officeID,SYNC_OP_MUTEX_LOCK,SYNC_ROLE_ACCOUNT,0);
    pthread_mutex_lock(&account_mutex[newAccount.account_id]);

    logDelay(getLogfile(),officeID,delay);
    usleep(MS_TO_US(delay));

    accounts[newAccount.account_id] = newAccount;

    logAccountCreation(getLogfile(), pthread_self(), &accounts[newAccount.account_id]);

    pthread_mutex_unlock(&account_mutex[newAccount.account_id]);
    logSyncMech(getLogfile(),officeID,SYNC_OP_MUTEX_UNLOCK,SYNC_ROLE_ACCOUNT,0);

    return 0;
}

bool existsBankAccount(uint32_t id)
{
    if (id >= MAX_BANK_ACCOUNTS) {
        print_location();
        return false;
    }

    if (accounts[id].account_id != ERROR_ACCOUNT_ID) {
        print_location();
        return true;
    }

    print_location();
    return false;
}

bank_account_t* findBankAccount(uint32_t id)
{   
    return id >= MAX_BANK_ACCOUNTS ? NULL : &accounts[id];
}

int initAccounts()
{
    for (size_t i = 0; i < MAX_BANK_ACCOUNTS; i++)
    {
        pthread_mutex_init(&account_mutex[i], NULL);
        accounts[i] = errorAccount();
    }

    return 0;
}

bank_account_t errorAccount()
{

    bank_account_t error;

    error.account_id = ERROR_ACCOUNT_ID;

    return error;
}